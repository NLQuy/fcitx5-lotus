/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-monitor.h
 * @brief Check of the uinput server sockets the addon connects to.
 */

#ifndef _FCITX5_LOTUS_MONITOR_H_
#define _FCITX5_LOTUS_MONITOR_H_

/**
 * @brief Checks that a connected uinput server socket is held by the server.
 *
 * The abstract socket names carry no permissions, so anyone could bind them first. The server runs
 * as LOTUS_UINPUT_PROXY_USER, so the peer must be that user or root; with LOTUS_SOCKET_NAMESPACE set
 * (the sockets of a test) the addon's own user is accepted too.
 * @param sock Connected socket.
 * @return True when the peer may be trusted.
 */
bool authenticateServerPeer(int sock);

#endif // _FCITX5_LOTUS_MONITOR_H_
