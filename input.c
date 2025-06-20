/*
 * This file is part of cappls, a screen recorder.
 * Copyright (C) 2025 Joe Desmond
 *
 * cappls is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * cappls is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with cappls.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "lib.h"
#include "input.h"

static HHOOK ll_keyboard_hook = { 0 };
static BOOL ctrl_pressed = FALSE;
static BOOL shift_pressed = FALSE;
static BOOL period_pressed = FALSE;
static BOOL combo_active = FALSE;

static LRESULT CALLBACK ll_keyboard_proc(int n_code, WPARAM w_param, LPARAM l_param) {
	if (n_code >= 0) {
		KBDLLHOOKSTRUCT * kb_info = (KBDLLHOOKSTRUCT *)l_param;
		BOOL key_down = w_param == WM_KEYDOWN || w_param == WM_SYSKEYDOWN;
		BOOL key_up = w_param == WM_KEYUP || w_param == WM_SYSKEYUP;

		if (key_down || key_up) {
			switch (kb_info->vkCode) {
				case VK_SHIFT:
				case VK_LSHIFT:
				case VK_RSHIFT:
					shift_pressed = key_up;
					break;
				case VK_CONTROL:
				case VK_LCONTROL:
				case VK_RCONTROL:
					ctrl_pressed = key_up;
					break;
				case VK_OEM_PERIOD:
					period_pressed = key_up;
					break;
			}
		}

		if (ctrl_pressed && shift_pressed && period_pressed) {
			if (! combo_active) {
				combo_active = TRUE;
				on_combo_pressed();
			}
		} else {
			combo_active = FALSE;
		}
	}

	return CallNextHookEx(ll_keyboard_hook, n_code, w_param, l_param);
}

void install_hook() {
	ll_keyboard_hook = SetWindowsHookEx(
		WH_KEYBOARD_LL,
		ll_keyboard_proc,
		NULL,
		0
	);
	check_err(! ll_keyboard_hook);
}

void uninstall_hook() {
	if (! ll_keyboard_hook) {
		return;
	}

	UnhookWindowsHookEx(ll_keyboard_hook);
}

void process_messages() {
	MSG msg;

	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}