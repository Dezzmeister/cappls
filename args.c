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
#include "args.h"
#include "logging.h"

int get_opt(int argc, const wchar_t * argv[], const wchar_t * opt) {
	for (int i = 0; i < argc; i++) {
		const wchar_t * arg = argv[i];

		if (wstr_eq(arg, opt)) {
			return i;
		}
	}

	return -1;
}

const wchar_t * get_arg(int argc, const wchar_t * argv[], const wchar_t * arg_name) {
	wchar_t buf[256] = { 0 };

	int name_len = lstrlenW(arg_name);
	int buf_len = name_len + 1;

	if (buf_len >= ARR_SIZE(buf)) {
		log_err(L"Arg name is too long\n");
		return NULL;
	}

	copy_wstr(buf, arg_name);
	buf[name_len] = L'=';

	for (int i = 0; i < argc; i++) {
		const wchar_t * arg = argv[i];
		int idx = find_wstr(arg, buf);

		if (idx != -1) {
			return arg + buf_len;
		}
	}

	return NULL;
}

int get_non_opt(int argc, const wchar_t * argv[], int start_idx) {
	int i = start_idx;

	while (i < argc) {
		const wchar_t * arg = argv[i];

		if (arg[0] != L'\0' && arg[0] != L'-') {
			return i;
		}

		i++;
	}

	return -1;
}

struct convert_result wstr_to_ui(const wchar_t * str) {
	struct convert_result out = {
		.ui = 0,
		.end_idx = 0,
		.is_valid = FALSE
	};

	int i = 0;
	wchar_t c;

	while ((c = str[i]) != L'\0' && c >= L'0' && c <= L'9') {
		out.is_valid = TRUE;
		out.ui = (out.ui * 10) + (c - L'0');
		i++;
	}

	out.end_idx = i;

	return out;
}
