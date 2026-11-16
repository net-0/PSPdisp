/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * console.c - Console of the desktop programs       *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * On Windows, the desktop programs are GUI programs (linked with -mwindows, see their Makefiles), so they open no console window,
 * when started from the Start menu, at sign-in, or by the desktop app (the daemon).
 * Started from a console (e.g. cmd or Windows Terminal), they write their logs to it, as a console program does, though the console doesn't wait for them.
 * From MSYS2 (mintty), the pipes of the terminal are already their standard output and error, so nothing changes there.
 *
 * Elsewhere, the programs are console programs already, and this does nothing.
 */

#pragma once

#if defined(_WIN32)
	#include <winsock2.h> // Before <windows.h>, or its <winsock.h> clashes with the <winsock2.h> of GIO (e.g. <gio/gnetworking.h> in wlan.c)
	#include <windows.h>
	#include <stdio.h>
#endif

/**
 * @brief Write the standard output and error to the console the program was started from, if it has none and there's one.
 */
void pspdisp_console_attach() {
#if defined(_WIN32)
	// Already given (e.g. the pipes of MSYS2, or a redirection to a file), or no console started it (e.g. the Start menu)
	HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
	if ((error != NULL && error != INVALID_HANDLE_VALUE) || !AttachConsole(ATTACH_PARENT_PROCESS)) return;

	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
#endif
}
