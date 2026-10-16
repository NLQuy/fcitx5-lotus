// SPDX-License-Identifier: GPL-3.0-or-later
//
// A key typed while a replacement is deleting is buffered and replayed once the
// replacement commits. Wayland apps behind GNOME Shell (text-input-v3)
// double-buffer commit_string until the next done event, so two commits sent
// back to back keep only the last one: "lâu" typed fast came out as "lu" and
// "đi" as "i". The replacement and the replayed text must reach the app as a
// single commit.
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
                          "Smooth buffered-key handling accepted or rejected the key unexpectedly");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-replay-single-commit");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        reportFailure("configure Smooth/Telex", "mode=Smooth, input method=Telex", "configured mode or input method differs",
                      "the replay test cannot exercise Smooth Telex behavior");
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

    // "la" reaches the app as plain keys; Telex a turns it into "lâ" through a
    // backspace replacement.
    if (!send(engine, entry, *context, FcitxKey_l, false) || !send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_a, true))
        return 1;
    KbMsg msg{};
    if (!listener.receive(msg, "the Telex replacement did not reach the uinput server"))
        return 1;
    if (msg.op != KbOp::Backspace || msg.count == 0) {
        reportFailure("receive replacement request", "op=0 (backspace), count > 0", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                      "Smooth mode did not request a backspace replacement");
        return 1;
    }

    // u is typed before the echoed backspaces come back, so it is buffered.
    if (!send(engine, entry, *context, FcitxKey_u, true))
        return 1;
    for (size_t i = 0; i < msg.count; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == msg.count))
            return 1;
    }

    const std::vector<std::string> expected{"âu"};
    if (context->commits() != expected) {
        std::string actual = "commits=";
        for (const auto& commit : context->commits())
            actual += "['" + commit + "']";
        reportFailure("verify replay commits", "commits=['âu']", actual,
                      "the replacement and the replayed key went out as separate commits, so a text-input-v3 app keeps only the last one");
        return 1;
    }
    return 0;
}
