/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file common-peer-check.h
 * @brief Socket peer checks shared by the addon and the uinput server.
 */

#ifndef _COMMON_PEER_CHECK_H_
#define _COMMON_PEER_CHECK_H_

#include <string_view>
#include <sys/types.h>

#include "common-export.h"

/**
 * @brief Whether a process status file shows a process inside a nested PID namespace.
 *
 * The NSpid line of /proc/<pid>/status lists the process ID in every PID namespace the process
 * belongs to, so more than one entry means a nested namespace, such as a Flatpak or bubblewrap
 * sandbox. Unlike /proc/<pid>/exe, the file is readable without ptrace access.
 * @param procStatus Contents of /proc/<pid>/status.
 * @return True when the NSpid line has more than one entry; false when it has one or is missing.
 */
// LOTUS_EXPORT: also read by the headless tests through lotus_test_core.
bool LOTUS_EXPORT isInNestedPidNamespace(std::string_view procStatus);

/**
 * @brief Whether the addon may trust the process holding a uinput server socket.
 *
 * The server runs as a dedicated proxy user, so only that user or root may hold the socket names;
 * anything else squatting them is ignored. Tests bind the sockets as the test user, so in test
 * mode the addon's own user is accepted too.
 * @param peerUid UID of the socket peer (SO_PEERCRED).
 * @param proxyUid UID of the proxy user, or (uid_t)-1 when the user does not exist.
 * @param ownUid UID of the addon process.
 * @param testMode True when the sockets live in a test namespace (LOTUS_SOCKET_NAMESPACE).
 * @return True when the peer is accepted.
 */
bool LOTUS_EXPORT isAllowedServerUid(uid_t peerUid, uid_t proxyUid, uid_t ownUid, bool testMode);

#endif // _COMMON_PEER_CHECK_H_
