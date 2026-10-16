/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "common-peer-check.h"
#include "common-socket-name.h"
#include "lotus-monitor.h"
#include "lotus-utils.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include <pwd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

    // UID of the user the uinput server runs as, or (uid_t)-1 when that user does not exist.
    uid_t proxyUid() {
        const long        bufSize = sysconf(_SC_GETPW_R_SIZE_MAX);
        std::vector<char> buffer(bufSize > 0 ? static_cast<std::size_t>(bufSize) : 16384U);
        struct passwd     pwd{};
        struct passwd*    result = nullptr;
        if (getpwnam_r(LOTUS_UINPUT_PROXY_USER, &pwd, buffer.data(), buffer.size(), &result) != 0 || result == nullptr) {
            return static_cast<uid_t>(-1);
        }
        return result->pw_uid;
    }

} // namespace

bool authenticateServerPeer(int sock) {
    struct ucred cred{};
    socklen_t    len = sizeof(cred);
    if (getsockopt(sock, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        LOTUS_ERROR("Failed to get the uinput server peer credentials: " + std::string(strerror(errno)));
        return false;
    }
    const bool testMode = !getEnv(LOTUS_SOCKET_NAMESPACE_ENV).empty();
    if (isAllowedServerUid(cred.uid, proxyUid(), getuid(), testMode)) {
        return true;
    }
    LOTUS_WARN("Refusing a uinput server socket held by UID " + std::to_string(cred.uid));
    return false;
}
