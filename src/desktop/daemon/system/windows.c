/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * windows.c - What the daemon needs from Windows    *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * The system of the daemon on Windows (see `system.c`): the console events stop it, the priority class of the process is the one of all its threads, and an elevated token is an administrator.
 */

#pragma once

#include <windows.h>
#include <glib.h>
#include "../system.c" // Its declarations (when `system.c` includes this file, it already has them and this include is skipped)

// `SetConsoleCtrlHandler()` gives no data to its routine, so the handler of `pspdisp_daemon_system_watch_stop()` is kept here
GSourceFunc pspdisp_daemon_system_stop_handler = NULL; // Stops the daemon
gpointer pspdisp_daemon_system_stop_data = NULL; // Given to `pspdisp_daemon_system_stop_handler`

/**
 * @brief Call the handler that stops the daemon, on the main thread.
 *
 * @param data Unused.
 * @return Always `G_SOURCE_REMOVE`, it's queued again with the next console event.
 */
gboolean pspdisp_daemon_system_on_stop(gpointer data) {
	(void) data;
	pspdisp_daemon_system_stop_handler(pspdisp_daemon_system_stop_data);
	return G_SOURCE_REMOVE;
}

/**
 * @brief Handle a console event, called by Windows on a thread of its own: Ctrl+C, Ctrl+Break and the closing of the console stop the daemon on the main thread.
 *
 * When the console is closed, Windows ends the process soon after this returns, maybe before the daemon finished stopping.
 *
 * @param event The console event.
 * @return `TRUE` if it was handled.
 */
BOOL WINAPI pspdisp_daemon_system_on_console(DWORD event) {
	if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT) return FALSE;

	g_idle_add(pspdisp_daemon_system_on_stop, NULL);
	return TRUE;
}

/**
 * @brief Call a handler on the main thread on Ctrl+C and Ctrl+Break, and when the console is closed.
 *
 * @param handler Stops the daemon, it returns `G_SOURCE_CONTINUE`.
 * @param data Given to the handler.
 */
void pspdisp_daemon_system_watch_stop(GSourceFunc handler, gpointer data) {
	pspdisp_daemon_system_stop_handler = handler;
	pspdisp_daemon_system_stop_data = data;

	if (!SetConsoleCtrlHandler(pspdisp_daemon_system_on_console, TRUE))
		g_warning("%s(): failed to watch the console events, Ctrl+C ends the daemon without stopping it: error %lu", __FUNCTION__, GetLastError());
}

/**
 * @brief Set the priority class of the process, which all its threads have: above normal for a higher priority.
 *
 * @param higher If the priority is higher than the one of the other programs.
 */
void pspdisp_daemon_system_set_priority(bool higher) {
	if (!SetPriorityClass(GetCurrentProcess(), higher ? ABOVE_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS))
		g_debug("%s(): failed to set the priority class: error %lu", __FUNCTION__, GetLastError());
}

/**
 * @brief Get the ID of the process of the daemon.
 *
 * @return The process ID.
 */
int64_t pspdisp_daemon_system_pid() {
	return GetCurrentProcessId();
}

/**
 * @brief Tell if the daemon runs elevated, as an administrator through the User Account Control.
 *
 * @return `true` if its token is elevated.
 */
bool pspdisp_daemon_system_runs_as_administrator() {
	HANDLE token;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

	TOKEN_ELEVATION elevation = {};
	DWORD size = sizeof(elevation);
	bool elevated = GetTokenInformation(token, TokenElevation, &elevation, size, &size) && elevation.TokenIsElevated;

	CloseHandle(token);
	return elevated;
}
