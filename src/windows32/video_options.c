/** \file
 *
 *  \brief Windows video options window.
 *
 *  \copyright Copyright 2023-2024 Ciaran Anscomb
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

#include <windows.h>
#include <commctrl.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "ao.h"
#include "sound.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialogs.h"

static INT_PTR CALLBACK tv_controls_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND vo_window = NULL;

void windows32_vo_create_window(struct ui_sdl2_interface *uisdl2) {
	(void)uisdl2;
	vo_window = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DLG_TV_CONTROLS), windows32_main_hwnd, (DLGPROC)tv_controls_proc);

	HWND vo_volume = GetDlgItem(vo_window, IDC_SPIN_VOLUME);
	SendMessage(vo_volume, UDM_SETRANGE, 0, MAKELPARAM(150, 0));
	SendMessage(vo_volume, UDM_SETPOS, 0, 70);

	HWND vo_brightness = GetDlgItem(vo_window, IDC_SPIN_BRIGHTNESS);
	SendMessage(vo_brightness, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_brightness, UDM_SETPOS, 0, 50);

	HWND vo_contrast = GetDlgItem(vo_window, IDC_SPIN_CONTRAST);
	SendMessage(vo_contrast, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_contrast, UDM_SETPOS, 0, 50);

	HWND vo_saturation = GetDlgItem(vo_window, IDC_SPIN_SATURATION);
	SendMessage(vo_saturation, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_saturation, UDM_SETPOS, 0, 0);

	HWND vo_hue = GetDlgItem(vo_window, IDC_SPIN_HUE);
	SendMessage(vo_hue, UDM_SETRANGE, 0, MAKELPARAM(180, -179));
	SendMessage(vo_hue, UDM_SETPOS, 0, 0);

	HWND cbt_picture = GetDlgItem(vo_window, IDC_CB_PICTURE);
	for (unsigned i = 0; i < NUM_VO_PICTURE; i++) {
		SendMessage(cbt_picture, CB_ADDSTRING, 0, (LPARAM)vo_picture_name[i]);
	}

	HWND cbt_cmp_renderer = GetDlgItem(vo_window, IDC_CB_RENDERER);
	for (unsigned i = 0; vo_cmp_ccr_list[i].name; i++) {
		SendMessage(cbt_cmp_renderer, CB_ADDSTRING, 0, (LPARAM)vo_cmp_ccr_list[i].description);
	}

	HWND cbt_cmp_fs = GetDlgItem(vo_window, IDC_CB_FS);
	for (unsigned i = 0; i < NUM_VO_RENDER_FS; i++) {
		SendMessage(cbt_cmp_fs, CB_ADDSTRING, 0, (LPARAM)vo_render_fs_name[i]);
	}

	HWND cbt_cmp_fsc = GetDlgItem(vo_window, IDC_CB_FSC);
	for (unsigned i = 0; i < NUM_VO_RENDER_FSC; i++) {
		SendMessage(cbt_cmp_fsc, CB_ADDSTRING, 0, (LPARAM)vo_render_fsc_name[i]);
	}

	HWND cbt_cmp_system = GetDlgItem(vo_window, IDC_CB_SYSTEM);
	for (unsigned i = 0; i < NUM_VO_RENDER_SYSTEM; i++) {
		SendMessage(cbt_cmp_system, CB_ADDSTRING, 0, (LPARAM)vo_render_system_name[i]);
	}
}

void windows32_vo_show_window(struct ui_sdl2_interface *uisdl2) {
	(void)uisdl2;
	ShowWindow(vo_window, SW_SHOW);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - update values in UI

void windows32_vo_update_volume(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND vo_volume = GetDlgItem(vo_window, IDC_SPIN_VOLUME);
	SendMessage(vo_volume, UDM_SETPOS, 0, value);
}

void windows32_vo_update_brightness(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND vo_brightness = GetDlgItem(vo_window, IDC_SPIN_BRIGHTNESS);
	SendMessage(vo_brightness, UDM_SETPOS, 0, value);
}

void windows32_vo_update_contrast(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND vo_contrast = GetDlgItem(vo_window, IDC_SPIN_CONTRAST);
	SendMessage(vo_contrast, UDM_SETPOS, 0, value);
}

void windows32_vo_update_saturation(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND vo_saturation = GetDlgItem(vo_window, IDC_SPIN_SATURATION);
	SendMessage(vo_saturation, UDM_SETPOS, 0, value);
}

void windows32_vo_update_hue(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND vo_hue = GetDlgItem(vo_window, IDC_SPIN_HUE);
	SendMessage(vo_hue, UDM_SETPOS, 0, value);
}

void windows32_vo_update_picture(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND cbt_picture = GetDlgItem(vo_window, IDC_CB_PICTURE);
	SendMessage(cbt_picture, CB_SETCURSEL, value, 0);
}

void windows32_vo_update_ntsc_scaling(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND tb_ntsc_scaling = GetDlgItem(vo_window, IDC_BN_NTSC_SCALING);
	SendMessage(tb_ntsc_scaling, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}

void windows32_vo_update_cmp_renderer(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND cbt_cmp_renderer = GetDlgItem(vo_window, IDC_CB_RENDERER);
	SendMessage(cbt_cmp_renderer, CB_SETCURSEL, value, 0);
}

void windows32_vo_update_cmp_fs(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND cbt_cmp_fs = GetDlgItem(vo_window, IDC_CB_FS);
	SendMessage(cbt_cmp_fs, CB_SETCURSEL, value, 0);
}

void windows32_vo_update_cmp_fsc(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND cbt_cmp_fsc = GetDlgItem(vo_window, IDC_CB_FSC);
	SendMessage(cbt_cmp_fsc, CB_SETCURSEL, value, 0);
}

void windows32_vo_update_cmp_system(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND cbt_cmp_system = GetDlgItem(vo_window, IDC_CB_SYSTEM);
	SendMessage(cbt_cmp_system, CB_SETCURSEL, value, 0);
}

void windows32_vo_update_cmp_colour_killer(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	HWND tb_cmp_colour_killer = GetDlgItem(vo_window, IDC_BN_COLOUR_KILLER);
	SendMessage(tb_cmp_colour_killer, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - signal handlers

static INT_PTR CALLBACK tv_controls_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	// hwnd is the handle for the dialog window, i.e. vo_window
	switch (msg) {

	case WM_INITDIALOG:
		return TRUE;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->idFrom) {
		case IDC_SPIN_VOLUME:
			if (xroar.ao_interface) {
				HWND vo_volume = GetDlgItem(hwnd, IDC_SPIN_VOLUME);
				sound_set_volume(xroar.ao_interface->sound_interface, (int16_t)SendMessage(vo_volume, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_BRIGHTNESS:
			if (xroar.vo_interface) {
				HWND vo_brightness = GetDlgItem(hwnd, IDC_SPIN_BRIGHTNESS);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_brightness, (int16_t)SendMessage(vo_brightness, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_CONTRAST:
			if (xroar.vo_interface) {
				HWND vo_contrast = GetDlgItem(hwnd, IDC_SPIN_CONTRAST);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_contrast, (int16_t)SendMessage(vo_contrast, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_SATURATION:
			if (xroar.vo_interface) {
				HWND vo_saturation = GetDlgItem(hwnd, IDC_SPIN_SATURATION);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_saturation, (int16_t)SendMessage(vo_saturation, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_HUE:
			if (xroar.vo_interface) {
				HWND vo_hue = GetDlgItem(hwnd, IDC_SPIN_HUE);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_hue, (int16_t)SendMessage(vo_hue, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		default:
			break;
		}
		return TRUE;

	case WM_COMMAND:
		if (HIWORD(wParam) == CBN_SELCHANGE) {
			int id = LOWORD(wParam);
			HWND cb = (HWND)lParam;
			int value = SendMessage(cb, CB_GETCURSEL, 0, 0);

			switch (id) {
			case IDC_CB_PICTURE:
				xroar_set_picture(0, value);
				break;

			case IDC_CB_RENDERER:
				if (xroar.vo_interface) {
					vo_set_cmp_ccr(xroar.vo_interface, 1, value);
				}
				break;

			case IDC_CB_FS:
				if (xroar.vo_interface) {
					vo_set_cmp_fs(xroar.vo_interface, 0, value);
				}
				break;

			case IDC_CB_FSC:
				if (xroar.vo_interface) {
					vo_set_cmp_fsc(xroar.vo_interface, 0, value);
				}
				break;

			case IDC_CB_SYSTEM:
				if (xroar.vo_interface) {
					vo_set_cmp_system(xroar.vo_interface, 0, value);
				}
				break;

			default: break;
			}
		} else if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);

			switch (id) {
			case IDC_BN_NTSC_SCALING:
				if (xroar.vo_interface) {
					HWND tb_ntsc_scaling = GetDlgItem(hwnd, IDC_BN_NTSC_SCALING);
					int value = !(SendMessage(tb_ntsc_scaling, BM_GETCHECK, 0, 0) == BST_CHECKED);
					vo_set_ntsc_scaling(xroar.vo_interface, 1, value);
				}
				return FALSE;

			case IDC_BN_COLOUR_KILLER:
				if (xroar.vo_interface) {
					HWND tb_cmp_colour_killer = GetDlgItem(hwnd, IDC_BN_COLOUR_KILLER);
					int value = !(SendMessage(tb_cmp_colour_killer, BM_GETCHECK, 0, 0) == BST_CHECKED);
					vo_set_cmp_colour_killer(xroar.vo_interface, 1, value);
				}
				return FALSE;
			case IDOK:
			case IDCANCEL:
				ShowWindow(hwnd, SW_HIDE);
				return TRUE;

			default:
				break;
			}
		}
		break;

	default:
		break;
	}
	return FALSE;
}
