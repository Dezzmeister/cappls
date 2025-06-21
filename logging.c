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
#include "logging.h"

enum log_level log_level = Info;

void set_log_level(enum log_level lvl) {
	log_level = lvl;
}

void print_lvl_fmt(enum log_level lvl, const wchar_t * fmt_str, ...) {
	if (lvl > log_level) {
		return;
	}

	va_list args;
	va_start(args, fmt_str);

	LPWSTR msg = vfmt(fmt_str, args);
	va_end(args);

	HANDLE stream = lvl == Error ? std_err : std_out;

	WriteConsole(stream, msg, lstrlenW(msg), NULL, NULL);
	LocalFree(msg);
}
