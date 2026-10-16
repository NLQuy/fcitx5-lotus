/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "common-peer-check.h"

#include <cstddef>

bool isInNestedPidNamespace(std::string_view procStatus) {
    constexpr std::string_view kField = "NSpid:";
    std::size_t                start  = 0;
    while (start < procStatus.size()) {
        const std::size_t      end  = procStatus.find('\n', start);
        const std::string_view line = procStatus.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.starts_with(kField)) {
            std::size_t ids    = 0;
            bool        inWord = false;
            for (const char c : line.substr(kField.size())) {
                const bool space = c == ' ' || c == '\t';
                if (!space && !inWord) {
                    ++ids;
                }
                inWord = !space;
            }
            return ids > 1;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

bool isAllowedServerUid(uid_t peerUid, uid_t proxyUid, uid_t ownUid, bool testMode) {
    constexpr auto kNoUser = static_cast<uid_t>(-1);
    if (proxyUid != kNoUser && peerUid == proxyUid) {
        return true;
    }
    return peerUid == 0 || (testMode && peerUid == ownUid);
}
