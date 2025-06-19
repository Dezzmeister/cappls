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
#include "venc.h"
#include "com.h"

struct hw_encoder enc = { 0 };
struct d3d d3d = { 0 };
struct display disp = { 0 };
struct mf_state mf = {
	.h_d3d_device = INVALID_HANDLE_VALUE
};
struct mp4_file mp4 = { 0 };

BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
	switch (ctrl_type) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT: {
			exit_process(0);
			return TRUE;
		}
		default:
			return FALSE;
	}
}

void exit_process(UINT code) {
	free_mp4_file(&mp4);
	free_mf_state(&mf);
	free_display(&disp);
	free_d3d(&d3d);
	free_hw_encoder(&enc);

	UINT freed_obj_count = release_all_com_objs();

	print_fmt(L"Released %1!d! COM object(s)\n", freed_obj_count);

	MFShutdown();
	CoUninitialize();
	ExitProcess(code);
}

int wmain(DWORD argc, LPCWSTR argv[]) {
	SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	check_hresult(hr, L"Failed to initialize COM");

	init_venc();

	enc = select_encoder();
	if (! enc.status) {
		print_err_fmt(L"Failed to select an encoder\n");
		return 1;
	}
	print_fmt(L"Selected encoder: %1!s!\n", enc.name);

	d3d = select_dxgi_adapter(&enc);
	if (! d3d.status) {
		print_err_fmt(L"Failed to select a DXGI adapter\n");
		return 1;
	}
	print_fmt(L"Selected DXGI adapter: %1!s!\n", d3d.adapter_desc);

	disp = select_display(&d3d, 0);
	if (! disp.status) {
		print_err_fmt(L"Failed to select a display\n");
		return 1;
	}
	print_fmt(L"Selected display: 0\n");

	mf = activate_encoder(&d3d);
	if (! mf.status) {
		print_err_fmt(L"Failed to activate encoder\n");
		return 1;
	}

	prepare_for_streaming(&disp, &mf);

	mp4 = prepare_mp4_file(L"video.mp4");

	Sleep(20);
	capture_screen(&disp, &mf, &mp4, 5, 30);

	return 0;
}
