// SPDX-License-Identifier: GPL-3.0-or-later
//
// A replacement still waiting for its echoes must stop when the caret moves
// elsewhere. A click left the addon waiting (clearAllBuffers gives up while a
// replacement is in flight), so the keys typed after the click were held back,
// and a focus change never told the server to drop the keys it had queued, so
// they reached the newly focused window.
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <memory>
#include <string>

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

    std::unique_ptr<TestInputContext> focusedContext(TestInstance& testInstance, fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry) {
        auto context = std::make_unique<TestInputContext>(&testInstance.instance);
        context->focusIn();
        fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, focus);
        return context;
    }

    bool startReplacement(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, KbSocketListener& listener) {
        if (!send(engine, entry, context, FcitxKey_l, false) || !send(engine, entry, context, FcitxKey_a, false) || !send(engine, entry, context, FcitxKey_a, true))
            return false;
        KbMsg msg{};
        if (!listener.receive(msg, "the Telex replacement did not reach the uinput server"))
            return false;
        if (msg.op != KbOp::Backspace) {
            reportFailure("receive replacement request", "op=0 (backspace)", "op=" + std::to_string(static_cast<int>(msg.op)), "Smooth mode did not request backspaces");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-replacement-cancel");
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

    // A click during the replacement: the server drops its queued keys by
    // itself, and the addon drops the replacement and the keys typed for the
    // old caret position.
    {
        auto context = focusedContext(testInstance, engine, entry);
        if (!startReplacement(engine, entry, *context, listener) || !send(engine, entry, *context, FcitxKey_u, true))
            return 1;
        engine.handleMouseClick();
        // An echo already on its way passes through like any BackSpace, and new
        // keys reach the app at once.
        if (!send(engine, entry, *context, FcitxKey_BackSpace, false) || !send(engine, entry, *context, FcitxKey_x, false))
            return 1;
        if (!context->commits().empty()) {
            reportFailure("verify commits after the click", "no commits", "commits=" + std::to_string(context->commits().size()),
                          "the replacement or the keys typed before the click were committed at the new caret position");
            return 1;
        }
    }

    // Focus moves to another window during the replacement: the server is told
    // to drop the keys it still has queued.
    {
        auto first = focusedContext(testInstance, engine, entry);
        if (!startReplacement(engine, entry, *first, listener))
            return 1;
        first->focusOut();
        fcitx::InputContextEvent blur(first.get(), fcitx::EventType::InputContextFocusOut);
        engine.deactivate(entry, blur);
        auto  second = focusedContext(testInstance, engine, entry);
        KbMsg msg{};
        if (!listener.receive(msg, "the focus change did not reach the uinput server"))
            return 1;
        if (msg.op != KbOp::Cancel) {
            reportFailure("receive request after the focus change", "op=2 (cancel)", "op=" + std::to_string(static_cast<int>(msg.op)),
                          "the server was not told to drop the keys queued for the old window");
            return 1;
        }
    }
    return 0;
}
