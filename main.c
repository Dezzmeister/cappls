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
#include "args.h"
#include "input.h"
#include "logging.h"
#include <codecapi.h>

static struct args default_args = {
	.profile = eAVEncH264VProfile_High,
	.log_level = Info,
	.bitrate = 12000000,
	.fps = 60,
	.display = 0
};

static struct hw_encoder enc = { 0 };
static struct d3d d3d = { 0 };
static struct display disp = { 0 };
static struct mf_state mf = {
	.h_d3d_device = INVALID_HANDLE_VALUE
};
static struct mp4_file mp4 = { 0 };

static void print_usage(const wchar_t * exe_name) {
	log_info(
		L"Usage: %1!s! (FILE) [--profile=base|main|high] [--bitrate=BITRATE] [--fps=FPS] [--display=DISPLAY]\n"
		L"\n"
		L"Records the screen. MP4 video will be written to (FILE). Screen recording starts when CTRL+SHIFT+.\n"
		L"(ctrl + shift + period) is pressed, and ends when CTRL+SHIFT+. is pressed again.\n"
		L"Options can be provided in addition to the filename:\n"
		L"  --profile           Sets the H.264 encoding profile. Can be one of \"base\", \"main\", or \"high\".\n"
		L"                      Default: high\n"
		L"  --bitrate           Sets the average bitrate for the encoder.\n"
		L"                      Default: %2!d!\n"
		L"  --fps               Sets the target frames per second.\n"
		L"                      Default: %3!d!\n"
		L"  --display           Sets the display to record. Displays are ordered from 0, the primary display.\n"
		L"                      Default: %4!d!\n"
		L"  --log-level         Sets the log level. Log levels range from 0 (error) to 4 (debug):\n"
		L"                        0: Error\n"
		L"                        1: Warning\n"
		L"                        2: Info\n"
		L"                        3: Verbose\n"
		L"                        4: Debug\n"
		L"                      Default: 2\n",
		basename(exe_name),
		default_args.bitrate,
		default_args.fps,
		default_args.display
	);
	ExitProcess(0);
}

static void print_help_hint(const wchar_t * exe_name) {
	log_err(L"For help: %1!s! --help\n", basename(exe_name));
	ExitProcess(1);
}

void on_combo_pressed() {
	mp4.recording = !mp4.recording;

	if (mp4.recording) {
		log_info(L"Press CTRL+SHIFT+. (ctrl + shift + period) again to stop recording\n");
	}
}

static struct args get_args(int argc, const wchar_t * argv[]) {
	struct args args = default_args;

	if (argc < 2) {
		print_usage(argv[0]);
	}

	int help_opt_idx = get_opt(argc, argv, L"--help");
	if (help_opt_idx != -1) {
		print_usage(argv[0]);
	}

	int file_idx = get_non_opt(argc, argv, 1);
	if (file_idx == -1) {
		log_err(L"Filename was not provided\n");
		print_help_hint(argv[0]);
	}

	args.filename = argv[file_idx];

	const wchar_t * profile_opt = get_arg(argc, argv, L"--profile");
	if (profile_opt) {
		if (wstr_eq(profile_opt, L"base")) {
			args.profile = eAVEncH264VProfile_Base;
		} else if (wstr_eq(profile_opt, L"main")) {
			args.profile = eAVEncH264VProfile_Main;
		} else if (wstr_eq(profile_opt, L"high")) {
			args.profile = eAVEncH264VProfile_High;
		}
	}

	const wchar_t * bitrate_opt = get_arg(argc, argv, L"--bitrate");
	if (bitrate_opt) {
		struct convert_result bitrate_result = wstr_to_ui(bitrate_opt);

		if (bitrate_result.status) {
			args.bitrate = bitrate_result.ui;
		} else {
			log_err(L"--bitrate must be an unsigned int, received %1!s!\n", bitrate_opt);
			print_help_hint(argv[0]);
		}
	}

	const wchar_t * fps_opt = get_arg(argc, argv, L"--fps");
	if (fps_opt) {
		struct convert_result fps_result = wstr_to_ui(fps_opt);

		if (fps_result.status) {
			args.fps = fps_result.ui;
		} else {
			log_err(L"--fps must be an unsigned int, received %1!s!\n", fps_opt);
			print_help_hint(argv[0]);
		}
	}

	const wchar_t * display_opt = get_arg(argc, argv, L"--display");
	if (display_opt) {
		struct convert_result display_result = wstr_to_ui(display_opt);

		if (display_result.status) {
			args.display = display_result.ui;
		} else {
			log_err(L"--display must be an unsigned int, received %1!s!\n", display_opt);
			print_help_hint(argv[0]);
		}
	}

	const wchar_t * log_level_opt = get_arg(argc, argv, L"--log-level");
	if (log_level_opt) {
		struct convert_result log_level_result = wstr_to_ui(log_level_opt);

		if (log_level_result.status) {
			args.log_level = log_level_result.ui > Debug ? Debug : log_level_result.ui;
		} else {
			log_err(L"--log-level must be an unsigned int, received %1!s!\n", log_level_opt);
			print_help_hint(argv[0]);
		}
	}

	return args;
}

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
	switch (ctrl_type) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			log_info(L"Interrupted (code: %1!d!)\n", ctrl_type);
			exit_process(0);
			return TRUE;
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

	log_verbose(L"Released %1!d! COM object(s)\n", freed_obj_count);

	uninstall_hook();

	MFShutdown();
	CoUninitialize();
	ExitProcess(code);
}

int wmain(DWORD argc, LPCWSTR argv[]) {
	struct args args = get_args(argc, argv);
	set_log_level(args.log_level);

	SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	check_hresult(hr, L"Failed to initialize COM");

	init_venc();

	enc = select_encoder(&args);
	if (! enc.status) {
		log_err(L"Failed to select an encoder\n");
		return 1;
	}
	log_info(L"Selected encoder: %1!s!\n", enc.name);

	d3d = select_dxgi_adapter(&enc);
	if (! d3d.status) {
		log_err(L"Failed to select a DXGI adapter\n");
		return 1;
	}
	log_info(L"Selected DXGI adapter: %1!s!\n", d3d.adapter_desc);

	disp = select_display(&d3d);
	if (! disp.status) {
		log_err(L"Failed to select a display\n");
		return 1;
	}
	log_info(L"Selected display: 0\n");

	mf = activate_encoder(&d3d);
	if (! mf.status) {
		log_err(L"Failed to activate encoder\n");
		return 1;
	}

	prepare_for_streaming(&disp, &mf);

	mp4 = prepare_mp4_file(args.filename);

	// Wait a bit - D3D won't have a frame ready if we don't
	// TODO: Properly handle case where D3D doesn't have a frame ready
	Sleep(20);
	install_hook();
	log_info(L"Press CTRL+SHIFT+. (ctrl + shift + period) to start recording\n");
	capture_screen(&disp, &mf, &mp4);

	return 0;
}
