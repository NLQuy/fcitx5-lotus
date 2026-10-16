// SPDX-License-Identifier: GPL-3.0-or-later
//
// A replacement waits for its echoed keys, and every key typed meanwhile waits
// with it. When an echo never comes back (the server restarted, rejected the
// request, or the request was lost), nothing ended the wait: every later key
// was swallowed until the user pressed BackSpace. A watchdog now gives up once
// the request had ample time to come back. With the deletion incomplete it
// drops the replacement, with only the trigger key missing it commits it, and
// either way the waiting keys are replayed.
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <fcitx-utils/event.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()),
                          "the key was accepted or passed through unexpectedly");
            return false;
        }
        return true;
    }

    // Runs the fcitx event loop long enough for the replacement watchdog to fire.
    void runEventLoop(fcitx::Instance& instance, uint64_t usec) {
        auto& loop = instance.eventLoop();
        auto  exit = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + usec, 0, [&loop](fcitx::EventSourceTime*, uint64_t) {
            loop.exit();
            return true;
        });
        loop.exec();
    }

    std::string commitList(const TestInputContext& context) {
        std::string list;
        for (const auto& commit : context.commits())
            list += "['" + commit + "']";
        return list;
    }

    std::unique_ptr<TestInputContext> focusedContext(TestInstance& testInstance, fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry) {
        auto context = std::make_unique<TestInputContext>(&testInstance.instance);
        context->focusIn();
        fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, focus);
        return context;
    }

    constexpr uint64_t kWaitUsec = 1000000;

} // namespace

// The fcitx event loop cannot run again once it exited, so each scenario runs
// in its own process: no argument for a lost request, --trigger-missing for a
// request whose trigger key alone got lost.
int main(int argc, char** argv) {
    const bool triggerMissing = argc > 1 && std::string_view(argv[1]) == "--trigger-missing";
    configureTestPaths(triggerMissing ? "fcitx5-lotus-replacement-watchdog-trigger" : "fcitx5-lotus-replacement-watchdog");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        reportFailure("configure Smooth/Telex", "mode=Smooth, input method=Telex", "configured mode or input method differs", "the test cannot exercise Smooth Telex behavior");
        return 1;
    }

    KbSocketListener listener;
    if (!listener.valid())
        return 1;
    const fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");

    // No echo comes back: the app still holds "la", so the replacement is
    // dropped and the waiting "u" is replayed as a new word.
    if (!triggerMissing) {
        auto context = focusedContext(testInstance, engine, entry);
        if (!send(engine, entry, *context, FcitxKey_l, false) || !send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_a, true))
            return 1;
        KbMsg msg{};
        if (!listener.receive(msg, "the Telex replacement did not reach the uinput server"))
            return 1;
        if (!send(engine, entry, *context, FcitxKey_u, true))
            return 1;
        runEventLoop(testInstance.instance, kWaitUsec);
        if (commitList(*context) != "['u']") {
            reportFailure("wait for the watchdog without echoes", "commits=['u']", "commits=" + commitList(*context), "the lost replacement still held the waiting keys");
            return 1;
        }
        // Nothing waits any more: a plain key reaches the app at once.
        if (!send(engine, entry, *context, FcitxKey_n, false))
            return 1;
    } else {
        // The deleting BackSpace came back but the trigger did not: the app
        // already dropped the "a", so the replacement is committed.
        auto context = focusedContext(testInstance, engine, entry);
        if (!send(engine, entry, *context, FcitxKey_l, false) || !send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_a, true))
            return 1;
        KbMsg msg{};
        if (!listener.receive(msg, "the Telex replacement did not reach the uinput server"))
            return 1;
        if (msg.count != 2) {
            reportFailure("receive replacement request", "count=2", "count=" + std::to_string(msg.count), "the test expects one deleting BackSpace plus the trigger");
            return 1;
        }
        if (!send(engine, entry, *context, FcitxKey_BackSpace, false))
            return 1;
        runEventLoop(testInstance.instance, kWaitUsec);
        if (commitList(*context) != "['â']") {
            reportFailure("wait for the watchdog without the trigger", "commits=['â']", "commits=" + commitList(*context), "the replacement was not committed after its deletion");
            return 1;
        }
        if (!send(engine, entry, *context, FcitxKey_n, false))
            return 1;
    }
    return 0;
}
