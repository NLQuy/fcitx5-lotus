// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keys typed while a replacement is deleting are buffered and replayed after
// it commits. Shortcuts and keys without text (Ctrl+S, Enter, arrows, ...)
// must reach the app as keys, in order and after the replacement text. They
// used to be replayed through the engine, which rejects them, so Ctrl+S was
// committed as the letter "s", Enter as "\r", and arrows were dropped.
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, const fcitx::Key& key, bool requireAccepted) {
        fcitx::KeyEvent event(&context, key, false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + key.toString(), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()),
                          "the key was accepted or passed through unexpectedly");
            return false;
        }
        return true;
    }

    std::string describe(const std::vector<fcitx::ForwardKeyEvent>& events) {
        std::string text;
        for (const auto& event : events)
            text += "[" + event.rawKey().toString() + (event.isRelease() ? " up]" : " down]");
        return text;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-replay-non-text-keys");
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
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    if (!send(engine, entry, *context, fcitx::Key(FcitxKey_l), false) || !send(engine, entry, *context, fcitx::Key(FcitxKey_a), false) ||
        !send(engine, entry, *context, fcitx::Key(FcitxKey_a), true))
        return 1;
    KbMsg msg{};
    if (!listener.receive(msg, "the Telex replacement did not reach the uinput server"))
        return 1;

    // Ctrl+S and Enter arrive before the echoed backspaces: both wait.
    const fcitx::Key ctrlS(FcitxKey_s, fcitx::KeyState::Ctrl);
    const fcitx::Key enter(FcitxKey_Return);
    if (!send(engine, entry, *context, ctrlS, true) || !send(engine, entry, *context, enter, true))
        return 1;
    if (!context->commits().empty() || !context->forwarded().empty()) {
        reportFailure("buffer keys during the replacement", "no commits, no forwarded keys",
                      "commits=" + std::to_string(context->commits().size()) + ", forwarded=" + describe(context->forwarded()),
                      "a key typed during the replacement reached the app before the replacement");
        return 1;
    }
    for (size_t i = 0; i < msg.count; ++i) {
        if (!send(engine, entry, *context, fcitx::Key(FcitxKey_BackSpace), i + 1 == msg.count))
            return 1;
    }

    std::string commits;
    for (const auto& commit : context->commits())
        commits += "['" + commit + "']";
    if (commits != "['â']") {
        reportFailure("verify replay commits", "commits=['â']", "commits=" + commits, "a shortcut or Enter was committed as text");
        return 1;
    }
    const auto& forwarded   = context->forwarded();
    const bool  keysInOrder = forwarded.size() == 4 && forwarded[0].rawKey() == ctrlS && !forwarded[0].isRelease() && forwarded[1].rawKey() == ctrlS && forwarded[1].isRelease() &&
        forwarded[2].rawKey() == enter && !forwarded[2].isRelease() && forwarded[3].rawKey() == enter && forwarded[3].isRelease();
    if (!keysInOrder) {
        reportFailure("verify replayed keys", "forwarded=[Control+s down][Control+s up][Return down][Return up]", "forwarded=" + describe(forwarded),
                      "the shortcut and Enter did not reach the app as keys, in order");
        return 1;
    }
    return 0;
}
