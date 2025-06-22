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
	.display = 0,
	.pool_size = 4
};

static struct hw_encoder enc = { 0 };
static struct d3d d3d = { 0 };
static struct display disp = { 0 };
static struct mf_state mf = {
	.h_d3d_device = INVALID_HANDLE_VALUE
};
static struct mp4_file mp4 = { 0 };
volatile BOOL should_terminate = FALSE;
volatile BOOL is_recording = FALSE;
BOOL is_ready_to_record = FALSE;

static void print_usage(const wchar_t * exe_name) {
	log_info(
		L"Usage: %1!s! (FILE) [--profile=base|main|high] [--bitrate=BITRATE] [--fps=FPS] [--display=DISPLAY] "
		L"[--log-level=LOG_LEVEL] [--encoder=ENCODER] [--pool-size=SIZE]\n"
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
		L"                      Default: %5!d!\n"
		L"  --list-encoders     Lists all available H.264 hardware encoders. Does not accept an argument.\n"
		L"                      If --log-level is Verbose or higher, attributes will be printed for each encoder.\n"
		L"  --encoder           Sets the H.264 hardware encoder to use. The value of this argument should\n"
		L"                      be a GUID retrieved from `--list-encoders`.\n"
		L"                      By default, cappls tries to select an encoder based on vendor and merit.\n"
		L"                      Setting an `--encoder` forces cappls to use the given encoder or fail.\n"
		L"  --pool-size         Sets the size of the NV12 converter pool. NV12 converters accept BGRA8 samples\n"
		L"                      from the duplication API and produce NV12 samples to be fed into the H.264 encoder.\n"
		L"                      Default: %6!d!\n",
		basename(exe_name),
		default_args.bitrate,
		default_args.fps,
		default_args.display,
		default_args.log_level,
		default_args.pool_size
	);
	ExitProcess(0);
}

static void print_help_hint(const wchar_t * exe_name) {
	log_err(L"For help: %1!s! --help\n", basename(exe_name));
	ExitProcess(1);
}

void on_combo_pressed() {
	if (! is_ready_to_record) {
		return;
	}

	mp4.is_recording = !mp4.is_recording;

	if (mp4.is_recording) {
		log_info(L"Press CTRL+SHIFT+. (ctrl + shift + period) again to stop recording\n");
		is_recording = TRUE;
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

	const wchar_t * profile_arg = get_arg(argc, argv, L"--profile");
	if (profile_arg) {
		if (wstr_eq(profile_arg, L"base")) {
			args.profile = eAVEncH264VProfile_Base;
		} else if (wstr_eq(profile_arg, L"main")) {
			args.profile = eAVEncH264VProfile_Main;
		} else if (wstr_eq(profile_arg, L"high")) {
			args.profile = eAVEncH264VProfile_High;
		}
	}

	const wchar_t * bitrate_arg = get_arg(argc, argv, L"--bitrate");
	if (bitrate_arg) {
		struct convert_result bitrate_result = wstr_to_ui(bitrate_arg);

		if (bitrate_result.is_valid) {
			args.bitrate = bitrate_result.ui;
		} else {
			log_err(L"--bitrate must be an unsigned int, received %1!s!\n", bitrate_arg);
			print_help_hint(argv[0]);
		}
	}

	const wchar_t * fps_arg = get_arg(argc, argv, L"--fps");
	if (fps_arg) {
		struct convert_result fps_result = wstr_to_ui(fps_arg);

		if (fps_result.is_valid) {
			args.fps = fps_result.ui;
		} else {
			log_err(L"--fps must be an unsigned int, received %1!s!\n", fps_arg);
			print_help_hint(argv[0]);
		}
	}

	const wchar_t * display_arg = get_arg(argc, argv, L"--display");
	if (display_arg) {
		struct convert_result display_result = wstr_to_ui(display_arg);

		if (display_result.is_valid) {
			args.display = display_result.ui;
		} else {
			log_err(L"--display must be an unsigned int, received %1!s!\n", display_arg);
			print_help_hint(argv[0]);
		}
	}

	const wchar_t * log_level_arg = get_arg(argc, argv, L"--log-level");
	if (log_level_arg) {
		struct convert_result log_level_result = wstr_to_ui(log_level_arg);

		if (log_level_result.is_valid) {
			args.log_level = log_level_result.ui > Debug ? Debug : log_level_result.ui;
		} else {
			log_err(L"--log-level must be an unsigned int, received %1!s!\n", log_level_arg);
			print_help_hint(argv[0]);
		}
	}

	const wchar_t * encoder_arg = get_arg(argc, argv, L"--encoder");
	if (encoder_arg) {
		int len = lstrlenW(encoder_arg);

		if (len != 36) {
			log_err(L"--encoder must be a 36-character GUID, received %1!s!\n", encoder_arg);
			print_help_hint(argv[0]);
		}

		// Can't convert this to CLSID until COM is initialized
		args.encoder_clsid_str[0] = L'{';
		copy_wstr(args.encoder_clsid_str + 1, encoder_arg);
		args.encoder_clsid_str[37] = L'}';
	}

	const wchar_t * pool_size_arg = get_arg(argc, argv, L"--pool-size");
	if (pool_size_arg) {
		struct convert_result pool_size_result = wstr_to_ui(pool_size_arg);

		if (pool_size_result.is_valid) {
			args.pool_size = pool_size_result.ui;

			if (args.pool_size > 32) {
				log_err(L"--pool-size cannot be greater than 32, received %1!s!\n", pool_size_arg);
				print_help_hint(argv[0]);
			}
		} else {
			log_err(L"--pool-size must be an unsigned int, received %1!s!\n", pool_size_arg);
			print_help_hint(argv[0]);
		}
	}

	int list_encoders_idx = get_opt(argc, argv, L"--list-encoders");
	args.list_encoders = list_encoders_idx != -1;

	int file_idx = get_non_opt(argc, argv, 1);
	if (file_idx == -1 && ! args.list_encoders) {
		log_err(L"Filename was not provided\n");
		print_help_hint(argv[0]);
	}

	args.filename = argv[file_idx];

	return args;
}

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
	switch (ctrl_type) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT: {
			log_info(L"Interrupted (code: %1!d!)\n", ctrl_type);
			if (is_recording) {
				should_terminate = TRUE;
			} else {
				exit_process(0);
			}
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

	if (args.list_encoders) {
		list_encoders();

		return 0;
	}

	enc = select_encoder(&args);
	if (! enc.is_initialized) {
		log_err(L"Failed to select an encoder\n");
		return 1;
	}
	log_info(L"Selected encoder: %1!s!\n", enc.name);

	d3d = select_dxgi_adapter(&enc);
	if (! d3d.is_initialized) {
		log_err(L"Failed to select a DXGI adapter\n");
		return 1;
	}
	log_info(L"Selected DXGI adapter: %1!s!\n", d3d.adapter_desc);

	disp = select_display(&d3d);
	if (! disp.is_initialized) {
		log_err(L"Failed to select a display\n");
		return 1;
	}
	log_info(L"Selected display: 0\n");

	mf = activate_encoder(&d3d);
	if (! mf.is_initialized) {
		log_err(L"Failed to activate encoder\n");
		return 1;
	}

	prepare_for_streaming(&disp, &mf);

	mp4 = create_mp4_file(&mf, args.filename);

	// Wait a bit - D3D won't have a frame ready if we don't
	// TODO: Properly handle case where D3D doesn't have a frame ready
	Sleep(20);
	install_hook();
	log_info(L"Press CTRL+SHIFT+. (ctrl + shift + period) to start recording\n");
	capture_screen(&disp, &mf, &mp4, &should_terminate, &is_ready_to_record);

	return 0;
}
