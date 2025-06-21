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
#pragma once
#include "lib.h"

#define log_err(...)							print_lvl_fmt(Error, __VA_ARGS__)
#define log_warn(...)							print_lvl_fmt(Warning, __VA_ARGS__)
#define log_info(...)							print_lvl_fmt(Info, __VA_ARGS__)
#define log_verbose(...)						print_lvl_fmt(Verbose, __VA_ARGS__)
#define log_debug(...)							print_lvl_fmt(Debug, __VA_ARGS__)

enum log_level {
	Error = 0,
	Warning,
	Info,
	Verbose,
	Debug
};

extern enum log_level log_level;

void set_log_level(enum log_level lvl);
void print_lvl_fmt(enum log_level lvl, const wchar_t * fmt_str, ...);