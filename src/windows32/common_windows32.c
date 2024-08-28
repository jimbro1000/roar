/** \file
 *
 *  \brief Windows user-interface common functions.
 *
 *  \copyright Copyright 2006-2024 Ciaran Anscomb
 *
 *  \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 *  XRoar is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later
 *  version.
 *
 *  See COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 */

#include "top-config.h"

/* Windows has a habit of making include order important: */
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <commctrl.h>

#include "xalloc.h"

#include "logging.h"

#include "windows32/common_windows32.h"
#include "windows32/guicon.h"

HWND windows32_main_hwnd = NULL;

/** A console window is created if requested, thus this should be called _after_
 * processing options that may call for a console, but _before_ generating any
 * output that should go to that console.
 *
 * Performs various incantations that seem to be required to make networking
 * code work.
 */

int windows32_init(_Bool alloc_console) {
	if (alloc_console) {
		redirect_io_to_console(1024);
	}
	// Windows needs this to do networking
	WORD wVersionRequested;
	WSADATA wsaData;
	wVersionRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVersionRequested, &wsaData) != 0) {
		LOG_WARN("windows32: WSAStartup failed\n");
		return -1;
	}
	return 0;
}

void windows32_shutdown(void) {
	WSACleanup();
}

void windows32_drawtext_path(HWND hWnd, LPDRAWITEMSTRUCT pDIS) {
	int length = SendMessage(hWnd, WM_GETTEXTLENGTH, 0, 0) + 1;
	char *text = xmalloc(length);
	length = SendMessage(hWnd, WM_GETTEXT, length, (LPARAM)text);
	FillRect(pDIS->hDC, &pDIS->rcItem, (HBRUSH)(COLOR_WINDOW+1));
	DrawText(pDIS->hDC, text, length, &pDIS->rcItem, DT_PATH_ELLIPSIS);
	free(text);
}

LRESULT windows32_send_message_dlg_item(HWND hDlg, int nIDDlgItem, UINT Msg,
					WPARAM wParam, LPARAM lParam) {
	HWND hWnd = GetDlgItem(hDlg, nIDDlgItem);
	return SendMessage(hWnd, Msg, wParam, lParam);
}
