/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành 
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef _COMMON_PROTOCOL_H_
#define _COMMON_PROTOCOL_H_

#include <cstddef>
#include <cstdint>

/**
 * @brief Keyboard socket operation requested by the fcitx5 addon.
 */
enum class KbOp : int8_t {
    Backspace = 0, ///< emit count BackSpace key events
    Select    = 1, ///< select count characters via Shift+Left
    Cancel    = 2, ///< drop the keys still queued from earlier requests (count unused)
};

/**
 * @brief Message on the keyboard socket from the fcitx5 addon to uinput server.
 */
struct KbMsg {
    KbOp     op;         ///< requested operation
    size_t   count;      ///< number of backspaces / characters to select
    uint32_t pre_delay;  ///< ms to wait BEFORE sending any key (commit_interval: cooldown between commits)
    uint32_t interval;   ///< ms between consecutive injected keys (backspace_interval)
    uint32_t post_delay; ///< ms to wait BEFORE sending the final trigger key (to let DOM expand selection)
};

#endif // _COMMON_PROTOCOL_H_
