// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preedit mode must hand BackSpace to the engine as the fcitx key symbol the
// engine compares against (kFcitxBackSpace), not as its control code point
// 0x08. With 0x08 the engine treats BackSpace as a key it cannot process: the
// whole preedit is committed and BackSpace reaches the app, so "tieng",
// BackSpace, "e" gives "tiene" instead of "tiên".
#include "lotus-engine.h"
#include "test-input-context.h"

#include <iostream>
#include <memory>
#include <string>

namespace {

    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-preedit-backspace");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Preedit || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        reportFailure("configure Preedit/Telex", "mode=Preedit, input method=Telex", "configured mode or input method differs", "the test cannot exercise Preedit Telex behavior");
        return 1;
    }

    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    // Every key, BackSpace included, edits the preedit and must not reach the app.
    for (const auto sym : {FcitxKey_t, FcitxKey_i, FcitxKey_e, FcitxKey_n, FcitxKey_g, FcitxKey_BackSpace, FcitxKey_e}) {
        fcitx::KeyEvent event(context.get(), fcitx::Key(sym), false);
        engine.keyEvent(entry, event);
        if (!event.accepted()) {
            reportFailure("type key " + std::to_string(sym), "accepted=1", "accepted=0, commits=" + std::to_string(context->commits().size()),
                          "the key left the preedit and reached the app");
            return 1;
        }
    }

    const std::string preedit = context->inputPanel().clientPreedit().toString();
    if (!context->commits().empty() || preedit != "tiên") {
        std::string commits;
        for (const auto& commit : context->commits())
            commits += "['" + commit + "']";
        reportFailure("verify preedit after BackSpace", "no commits, preedit 'tiên'", "commits=" + commits + ", preedit '" + preedit + "'",
                      "BackSpace committed the word instead of deleting its last letter");
        return 1;
    }
    return 0;
}
