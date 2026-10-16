// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file peer-check.cpp
 * @brief Unit test for the socket peer checks: the server keeps sandboxed processes away by their
 *        PID namespace, and the addon only trusts a server socket held by the proxy user or root.
 */

#include "common-peer-check.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {

    bool fail(const std::string& step, bool expected, bool actual) {
        std::cerr << "Step: " << step << '\n'
                  << "Expected: " << (expected ? "true" : "false") << '\n'
                  << "Actual: " << (actual ? "true" : "false") << '\n'
                  << "Meaning: a socket peer was accepted or rejected incorrectly\n";
        return false;
    }

    bool expectNested(std::string_view status, bool want) {
        const bool got = isInNestedPidNamespace(status);
        return got == want || fail("isInNestedPidNamespace(\"" + std::string(status) + "\")", want, got);
    }

    bool expectServer(uid_t peer, uid_t proxy, uid_t own, bool testMode, bool want) {
        const bool got = isAllowedServerUid(peer, proxy, own, testMode);
        return got == want ||
            fail("isAllowedServerUid(" + std::to_string(peer) + ", " + std::to_string(proxy) + ", " + std::to_string(own) + ", " + (testMode ? "true" : "false") + ")", want, got);
    }

} // namespace

int main() {
    bool ok = true;

    // A process in the host namespace has one NSpid entry; a sandboxed one has one per level.
    ok &= expectNested("Name:\tfcitx5\nPid:\t2177\nNSpid:\t2177\nNSpgid:\t2177\n", false);
    ok &= expectNested("Name:\tbwrap-child\nNSpid:\t5123\t2\n", true);
    ok &= expectNested("NSpid:\t5123 7 2", true);
    ok &= expectNested("Name:\tx\nNSpid:\t2177", false);
    // Kernels before 4.1 have no NSpid line: nothing shows a nested namespace.
    ok &= expectNested("Name:\tx\nPid:\t2177\n", false);
    ok &= expectNested("", false);
    // NSpgid and NSsid are other fields and must not be read as NSpid.
    ok &= expectNested("NSpgid:\t5123\t2\nNSpid:\t5123\n", false);

    constexpr uid_t kProxy  = 981;
    constexpr uid_t kUser   = 1000;
    constexpr uid_t kOther  = 1001;
    constexpr auto  kNoUser = static_cast<uid_t>(-1);
    // The proxy user and root may hold the server sockets.
    ok &= expectServer(kProxy, kProxy, kUser, false, true);
    ok &= expectServer(0, kProxy, kUser, false, true);
    // Another user, or the addon's own user outside of tests, may not.
    ok &= expectServer(kOther, kProxy, kUser, false, false);
    ok &= expectServer(kUser, kProxy, kUser, false, false);
    // Tests bind the sockets as the test user.
    ok &= expectServer(kUser, kProxy, kUser, true, true);
    ok &= expectServer(kOther, kProxy, kUser, true, false);
    // Without a proxy user only root is trusted, and (uid_t)-1 never matches it.
    ok &= expectServer(kNoUser, kNoUser, kUser, false, false);
    ok &= expectServer(0, kNoUser, kUser, false, true);
    return ok ? 0 : 1;
}
