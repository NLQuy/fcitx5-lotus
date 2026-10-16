/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-abstract-socket.h"
#include "lotus-server.h"
#include "lotus-logger.h"
#include "common-executable-path.h"
#include "common-fd-guard.h"
#include "common-protocol.h"
#include "common-socket-name.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <span>
#include <thread>
#include <utility>
#include <vector>
#include <csignal>
#include <cstddef>

#include <climits> // IWYU pragma: keep
#include <sched.h>
#include <unistd.h>

std::atomic<bool> g_running{true};

bool              UinputDevice::initialize() {
    dev_ = buildLotusUinputDevice();
    if (dev_ == nullptr) {
        LotusLogger::instance().error("Failed to build the uinput device description");
        return false;
    }

    libevdev_uinput* raw = nullptr;
    const int        err = libevdev_uinput_create_from_device(dev_.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &raw);
    if (err != 0) {
        LotusLogger::instance().error("Failed to create the uinput device: " + std::string(strerror(-err)));
        return false;
    }
    uidev_.reset(raw);
    if (const char* node = libevdev_uinput_get_devnode(uidev_.get()); node != nullptr) {
        for (int waited = 0; waited < 1000 && access(node, R_OK) != 0; waited += 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        LotusLogger::instance().info("Uinput device ready: " + std::string(node));
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        LotusLogger::instance().info("Uinput device ready (device node unknown)");
    }
    return true;
}

void UinputDevice::reportWriteFailure(int error) {
    static bool reported = false;
    if (!reported) {
        reported = true;
        LotusLogger::instance().error("Failed to write an uinput event: " + std::string(strerror(-error)));
    }
}

void UinputDevice::send_tap(uint16_t code) {
    if (uidev_ == nullptr) {
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_KEY, code, 1); err != 0) {
        reportWriteFailure(err);
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_SYN, SYN_REPORT, 0); err != 0) {
        reportWriteFailure(err);
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_KEY, code, 0); err != 0) {
        reportWriteFailure(err);
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_SYN, SYN_REPORT, 0); err != 0) {
        reportWriteFailure(err);
    }
}

void UinputDevice::send_mod(uint16_t code, int value) {
    if (uidev_ == nullptr) {
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_KEY, code, value); err != 0) {
        reportWriteFailure(err);
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_SYN, SYN_REPORT, 0); err != 0) {
        reportWriteFailure(err);
    }
}

void UinputDevice::send_backspace() {
    send_tap(KEY_BACKSPACE);
}

void UinputDevice::send_delete() {
    send_tap(KEY_DELETE);
}

void UinputDevice::send_shift_down() {
    send_mod(KEY_LEFTSHIFT, 1);
}

void UinputDevice::send_shift_up() {
    send_mod(KEY_LEFTSHIFT, 0);
}

void UinputDevice::send_left() {
    send_tap(KEY_LEFT);
}

LibinputContext::LibinputContext(const struct libinput_interface* interface) : udev_(udev_new()) {
    if (udev_ == nullptr) {
        return;
    }
    li_.reset(libinput_udev_create_context(interface, nullptr, udev_.get()));
    if (li_ != nullptr && libinput_udev_assign_seat(li_.get(), "seat0") != 0) {
        li_.reset();
    }
}

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running.store(false);
    }
}

std::string get_current_username() {
    struct passwd  pwd{};
    struct passwd* result   = nullptr;
    long           buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buf_size == -1) {
        buf_size = 16384;
    }
    std::vector<char> buffer(static_cast<std::size_t>(buf_size));
    if (getpwuid_r(getuid(), &pwd, buffer.data(), buffer.size(), &result) != 0 || result == nullptr) {
        return "unknown";
    }
    return {result->pw_name};
}

std::optional<uid_t> get_uid_for_user(std::string_view username) {
    const std::string      name(username);
    struct passwd          pw_buf{};
    struct passwd*         pw = nullptr;
    std::array<char, 1024> buffer{};
    if (getpwnam_r(name.c_str(), &pw_buf, buffer.data(), buffer.size(), &pw) != 0 || pw == nullptr) {
        return std::nullopt;
    }
    return pw->pw_uid;
}

void boost_process_priority() {
    if (setpriority(PRIO_PROCESS, 0, -10) != 0) { //NOLINT
        LotusLogger::instance().error("Failed to boost process priority");
    }
}

void pin_to_pcore() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i <= 3; ++i)
        CPU_SET(i, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        LotusLogger::instance().error("Failed to pin process to core");
    }
}

int open_restricted(const char* path, int flags, void* /*user_data*/) {
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

void close_restricted(int fd, void* /*user_data*/) {
    close(fd);
}

const struct libinput_interface interface = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

std::optional<pid_t> authorizePeer(int fd, uid_t expectedUid, std::string_view socketLabel) {
    struct ucred cred{};
    socklen_t    len = sizeof(struct ucred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        LotusLogger::instance().warn("Failed to get peer credentials for " + std::string(socketLabel));
        return std::nullopt;
    }
    if (cred.uid != expectedUid) {
        LotusLogger::instance().warn("Unauthorized UID connection attempt to " + std::string(socketLabel) + " from UID: " + std::to_string(cred.uid));
        return std::nullopt;
    }

    const std::string          exe_link = "/proc/" + std::to_string(cred.pid) + "/exe";
    std::array<char, PATH_MAX> exe_path{};
    const ssize_t              length = ::readlink(exe_link.c_str(), exe_path.data(), exe_path.size() - 1);
    if (length <= 0) {
        LotusLogger::instance().warn("Failed to resolve the peer executable for " + std::string(socketLabel));
        return std::nullopt;
    }

    const std::string_view exe(exe_path.data(), static_cast<std::size_t>(length));
    if (!isAllowedExecutablePath(exe, LOTUS_FCITX5_EXECUTABLE, LOTUS_ALT_EXECUTABLE_PREFIX)) {
        LotusLogger::instance().warn("Unauthorized executable connection attempt to " + std::string(socketLabel) + " from: " + std::string(exe));
        return std::nullopt;
    }
    return cred.pid;
}

int main(int argc, char* argv[]) {
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    const std::string            target_user = args.size() == 3 && std::string_view(args[1]) == "-u" ? std::string(args[2]) : get_current_username();
    LotusLogger::instance().info("Target user: " + target_user);

    const std::optional<uid_t> expected_uid = get_uid_for_user(target_user);
    if (!expected_uid.has_value()) {
        LotusLogger::instance().error("Failed to find UID for target user: " + target_user);
        return 1;
    }

    boost_process_priority();
    pin_to_pcore();

    const std::string    socket_namespace = lotusSocketNamespace(target_user);
    const AbstractSocket kb_socket        = makeAbstractSocket("lotussocket-" + socket_namespace + "-kb_socket");
    const AbstractSocket mouse_socket     = makeAbstractSocket("lotussocket-" + socket_namespace + "-mouse_socket");

    // Setup Uinput
    UinputDevice uinput;
    if (!uinput.initialize()) {
        LotusLogger::instance().error("Failed to initialize uinput device");
        return 1;
    }

    FdGuard server_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));
    FdGuard mouse_server_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));

    if (bind(server_fd.get(), kb_socket.asSockaddr(), kb_socket.length) != 0) {
        LotusLogger::instance().error("Failed to bind socket");
        return 1;
    }

    if (bind(mouse_server_fd.get(), mouse_socket.asSockaddr(), mouse_socket.length) != 0) {
        LotusLogger::instance().error("Failed to bind socket");
        return 1;
    }

    listen(server_fd.get(), 5);
    listen(mouse_server_fd.get(), 5);

    LibinputContext li_ctx(&interface);
    if (!li_ctx.is_valid()) {
        LotusLogger::instance().error("Failed to create libinput/udev context");
        return 1;
    }

    constexpr std::size_t        kKeyboardServerIndex = 0U; ///< Slot of the keyboard socket server fd
    constexpr std::size_t        kMouseServerIndex    = 2U; ///< Slot of the mouse socket server fd
    constexpr std::size_t        kKbClientIndex       = 3U; ///< Slot of the connected keyboard socket client

    std::array<struct pollfd, 4> fds{
        {
            {.fd = server_fd.get(), .events = POLLIN, .revents = 0},
            {.fd = li_ctx.get_fd(), .events = POLLIN, .revents = 0},
            {.fd = mouse_server_fd.get(), .events = POLLIN, .revents = 0},
            {.fd = -1, .events = POLLIN, .revents = 0},
        },
    };

    FdGuard                               addon_fd;
    FdGuard                               kb_client_fd;
    size_t                                pending_backspaces = 0;
    size_t                                pending_selects    = 0;     ///< remaining Shift+Left events to pace out
    bool                                  shift_held         = false; ///< Shift pressed for an in-flight selection
    uint32_t                              current_post_delay = 0;     ///< stores post_delay from KbMsg
    uint32_t                              current_interval   = 5;     ///< ms between injected keys within one request
    std::chrono::steady_clock::time_point next_key_due{};             ///< when the next paced key is due

    // Drops every key still queued and releases a Shift held for a selection.
    const auto abortPending = [&] {
        if (shift_held) {
            uinput.send_shift_up(); // never leave Shift stuck down
            shift_held = false;
        }
        pending_backspaces = 0;
        pending_selects    = 0;
    };

    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    while (g_running.load(std::memory_order_acquire)) {
        int poll_timeout = -1;
        if (pending_backspaces > 0 || pending_selects > 0) {
            const auto wait = std::chrono::ceil<std::chrono::milliseconds>(next_key_due - std::chrono::steady_clock::now()).count();
            poll_timeout    = static_cast<int>(std::max<std::chrono::milliseconds::rep>(wait, 0));
        }
        int ret = poll(fds.data(), fds.size(), poll_timeout);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        // Paced keys follow absolute deadlines: typing, pointer motion and
        // socket traffic wake poll() too and must not push the next key back.
        if ((pending_backspaces > 0 || pending_selects > 0) && std::chrono::steady_clock::now() >= next_key_due) {
            if (pending_backspaces > 0) {
                if (pending_backspaces == 1 && current_post_delay > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_post_delay));
                }
                uinput.send_backspace();
                --pending_backspaces;
            } else if (pending_selects > 0) {
                if (pending_selects > 1) {
                    uinput.send_left();
                    --pending_selects;
                } else {
                    if (shift_held) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        uinput.send_shift_up();
                        shift_held = false;
                    } else {
                        // BackSpace, not Delete: while the user holds Shift (typing a
                        // capital), Shift+Delete means Cut in most toolkits and would
                        // overwrite the clipboard, while Shift+BackSpace only deletes.
                        uinput.send_backspace();
                        if (current_post_delay > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(current_post_delay));
                        }
                        uinput.send_left();
                        --pending_selects;
                    }
                }
            }
            next_key_due = std::chrono::steady_clock::now() + std::chrono::milliseconds(current_interval);
        }

        libinput_dispatch(li_ctx.get_li());

        // handle socket (backspace)
        if ((static_cast<unsigned>(fds[kKeyboardServerIndex].revents) & static_cast<unsigned>(POLLIN)) != 0) {
            int client_fd = accept4(server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (client_fd >= 0) {
                const std::optional<pid_t> peer = authorizePeer(client_fd, *expected_uid, "keyboard socket");

                if (peer.has_value()) {
                    LotusLogger::instance().info("Fcitx5 connected to keyboard socket (PID: " + std::to_string(*peer) + ")");
                    abortPending(); // keys queued for a previous fcitx5 are not this one's
                    kb_client_fd.reset(client_fd);
                    fds[kKbClientIndex].fd = kb_client_fd.get();
                } else {
                    close(client_fd);
                }
            }
        }

        // handle connect from addon
        if (fds[kKbClientIndex].fd >= 0 &&
            (static_cast<unsigned>(fds[kKbClientIndex].revents) & (static_cast<unsigned>(POLLIN) | static_cast<unsigned>(POLLHUP) | static_cast<unsigned>(POLLERR))) != 0) {
            KbMsg   msg{};
            ssize_t n = recv(fds[kKbClientIndex].fd, &msg, sizeof(msg), 0);
            if (n <= 0) {
                LotusLogger::instance().warn("Keyboard client disconnected or connection error");
                abortPending();
                kb_client_fd.reset(-1);
                fds[kKbClientIndex].fd = -1;
            } else if (std::cmp_not_equal(n, sizeof(KbMsg)) && std::cmp_not_equal(n, sizeof(int32_t))) {
                LotusLogger::instance().warn("Malformed keyboard message (" + std::to_string(n) + " bytes)");
            } else if (msg.op == KbOp::Cancel) {
                abortPending();
            } else {
                current_interval = msg.interval > 0 ? std::min<uint32_t>(msg.interval, 1000) : 5;
                if (msg.count == 1 && msg.op == KbOp::Select) {
                    // Shift is released before the last key of a selection, so a
                    // selection of one key would leave it held.
                    LotusLogger::instance().warn("Ignoring a selection of a single key");
                } else if (msg.count > 0 && msg.op == KbOp::Select) {
                    if (msg.pre_delay > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(msg.pre_delay));
                    }
                    current_post_delay = msg.post_delay;
                    if (!shift_held) {
                        uinput.send_shift_down();
                        shift_held = true;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_interval));
                    uinput.send_left();
                    pending_selects += msg.count - 1;
                    next_key_due = std::chrono::steady_clock::now() + std::chrono::milliseconds(current_interval);

                } else if (msg.count > 0 && msg.op == KbOp::Backspace) {
                    if (msg.pre_delay > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(msg.pre_delay));
                    }
                    current_post_delay = msg.post_delay;
                    if (shift_held) {
                        // A stale in-flight selection must not mix with backspaces.
                        uinput.send_shift_up();
                        shift_held      = false;
                        pending_selects = 0;
                    }
                    pending_backspaces += msg.count - 1;
                    uinput.send_backspace();
                    next_key_due = std::chrono::steady_clock::now() + std::chrono::milliseconds(current_interval);
                } else if (msg.count > 0) {
                    LotusLogger::instance().warn("Unknown keyboard message op: " + std::to_string(static_cast<int>(msg.op)));
                }
            }
        }

        // connect to mouse socket
        if ((static_cast<unsigned>(fds[kMouseServerIndex].revents) & static_cast<unsigned>(POLLIN)) != 0) {
            int new_fd = accept4(mouse_server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (new_fd >= 0) {
                const std::optional<pid_t> peer = authorizePeer(new_fd, *expected_uid, "mouse socket");

                if (peer.has_value()) {
                    LotusLogger::instance().info("New mouse flag client connected");
                    addon_fd.reset(new_fd);
                } else {
                    close(new_fd);
                }
            }
        }

        // handle mouse (libinput)
        // libinput_dispatch() may have already consumed the fd in an iteration where
        // the libinput revents were 0 (poll timeout, or another fd woke us up).  Always
        // drain the queue; libinput_get_event() returns nullptr when it is empty.
        while (LibinputEvent event{libinput_get_event(li_ctx.get_li())}) {
            switch (libinput_event_get_type(event.get())) {
                case LIBINPUT_EVENT_POINTER_BUTTON: {
                    struct libinput_event_pointer* pointer = libinput_event_get_pointer_event(event.get());
                    if (libinput_event_pointer_get_button_state(pointer) != LIBINPUT_BUTTON_STATE_PRESSED) {
                        break;
                    }
                    // A click moves the caret: keys still queued would land at the new spot.
                    abortPending();
                    if (addon_fd.is_valid() && send(addon_fd.get(), "C", 1, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
                        LotusLogger::instance().warn("Failed to send to mouse flag client, closing connection");
                        addon_fd.reset(-1);
                    }
                    break;
                }
                case LIBINPUT_EVENT_DEVICE_ADDED: {
                    struct libinput_device* device  = libinput_event_get_device(event.get());
                    const char* const       rawName = libinput_device_get_name(device);
                    const std::string_view  name    = rawName != nullptr ? std::string_view(rawName) : std::string_view{};
                    LotusLogger::instance().info("Device added: " + std::string(name));
                    if (libinput_device_config_tap_get_finger_count(device) > 0) {
                        libinput_device_config_tap_set_enabled(device, LIBINPUT_CONFIG_TAP_ENABLED);
                        libinput_device_config_tap_set_button_map(device, LIBINPUT_CONFIG_TAP_MAP_LRM);
                    }
                    break;
                }
                default: break;
            }
        }
    }
    LotusLogger::instance().info("Terminating server...");
    return 0;
}
