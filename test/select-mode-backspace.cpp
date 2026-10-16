// SPDX-License-Identifier: GPL-3.0-or-later
//
// Select mode echoes Shift+Left and Delete, never BackSpace, so a BackSpace
// that arrives during a selection comes from the user. It used to be counted
// as an echo, which finished the replacement early: the replacement was
// committed before the app had deleted the selection, and the remaining
// Delete and Left reached the app as ordinary keys. The BackSpace must wait
// and reach the app after the replacement.
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()),
                          "Select echo handling accepted or rejected the key unexpectedly");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-mode-backspace");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Select)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Select || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        reportFailure("configure Select/Telex", "mode=Select, input method=Telex", "configured mode or input method differs",
                      "the test cannot exercise Select mode Telex behavior");
        return 1;
    }

    KbSocketListener listener;
    if (!listener.valid())
        return 1;
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
        return 1;
    KbMsg msg{};
    if (!listener.receive(msg, "the Telex replacement did not reach the uinput server"))
        return 1;
    if (msg.op != KbOp::Select || msg.count != 2) {
        reportFailure("receive replacement request", "op=1 (select), count=2", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                      "the replacement was not a two-arrow selection");
        return 1;
    }

    // The user's BackSpace during the selection waits for the replacement.
    if (!send(engine, entry, *context, FcitxKey_BackSpace, true))
        return 1;
    if (!send(engine, entry, *context, FcitxKey_Left, false) || !send(engine, entry, *context, FcitxKey_Delete, false))
        return 1;
    if (!context->commits().empty()) {
        reportFailure("wait for the last echo", "no commit before the last Left", "commits=" + std::to_string(context->commits().size()),
                      "the user's BackSpace was counted as a selection echo");
        return 1;
    }
    if (!send(engine, entry, *context, FcitxKey_Left, true))
        return 1;

    const auto& forwarded          = context->forwarded();
    const bool  backspaceForwarded = forwarded.size() == 2 && forwarded[0].rawKey().sym() == FcitxKey_BackSpace && !forwarded[0].isRelease() &&
        forwarded[1].rawKey().sym() == FcitxKey_BackSpace && forwarded[1].isRelease();
    if (context->commits() != std::vector<std::string>{"á"} || !backspaceForwarded) {
        reportFailure("verify replacement and replayed BackSpace", "commits=['á'], forwarded=[BackSpace down][BackSpace up]",
                      "commits=" + std::to_string(context->commits().size()) + ", forwarded=" + std::to_string(forwarded.size()),
                      "the BackSpace did not reach the app after the replacement");
        return 1;
    }
    return 0;
}
