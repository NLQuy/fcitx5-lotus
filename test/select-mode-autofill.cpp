// SPDX-License-Identifier: GPL-3.0-or-later
//
// Select mode with an autofill suffix selected after the caret, like Chromium's
// address bar inline autocomplete. isAutofillCertain() flags it, but Select mode
// must still ask for a Shift+Left selection of the replaced characters plus the
// swallowed trigger arrow: the arrow grows the suffix selection leftward and the
// commit replaces it. Falling back to backspaces with the autofill compensation
// deletes one real character too many whenever the heuristic misfires, which is
// how "hiện" turns into "hện" in VS Code and Chrome.
#include "fcitx-utils/keysym.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

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
    configureTestPaths("fcitx5-lotus-select-mode-autofill");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Select)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Select || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        reportFailure("configure Select/Telex", "mode=Select, input method=Telex", "configured mode or input method differs",
                      "the autofill test cannot exercise Select mode Telex behavior");
        return 1;
    }

    KbSocketListener listener;
    if (!listener.valid())
        return 1;
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    // "ma" reaches the app as plain keys, then the address bar autocompletes
    // "magazine.com" and selects the suffix, leaving the caret after "ma".
    if (!send(engine, entry, *context, FcitxKey_m, false) || !send(engine, entry, *context, FcitxKey_a, false))
        return 1;
    context->surroundingText().setText("magazine.com", 2, 12);
    context->updateSurroundingText();

    // Telex s turns "a" into "á": one character to replace, so the request is a
    // select of 2, exactly as without autofill.
    if (!send(engine, entry, *context, FcitxKey_s, true))
        return 1;
    KbMsg msg{};
    if (!listener.receive(msg, "the replacement did not reach the uinput server"))
        return 1;
    if (msg.op != KbOp::Select || msg.count != 2) {
        reportFailure("receive replacement request", "op=1 (select), count=2", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                      "Select mode fell back to backspaces with the autofill compensation, which deletes one real character too many");
        return 1;
    }

    // Echoes: the first arrow extends the selection, Delete clears it and the
    // last arrow commits the replacement.
    if (!send(engine, entry, *context, FcitxKey_Left, false) || !send(engine, entry, *context, FcitxKey_Delete, false) || !send(engine, entry, *context, FcitxKey_Left, true))
        return 1;
    const std::vector<std::string> expected{"á"};
    if (context->commits() != expected) {
        std::string actual = "commits=";
        for (const auto& commit : context->commits())
            actual += "['" + commit + "']";
        reportFailure("verify commit after the last left arrow", "commits=['á']", actual, "the replacement was not committed exactly once when the selection completed");
        return 1;
    }
    return 0;
}
