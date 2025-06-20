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

// The result of a conversion from string
struct convert_result {
	union {
		unsigned int ui;
	};
	// Index of the first char in the string that could not
	// be converted
	int end_idx;
	// TRUE if the conversion was successful, FALSE is not
	BOOL status : 1;
};

// Returns the index of a string in the args array. Unlike POSIX's `getopt`, this
// function tries to find the given string in the array of arguments.
// Example (finding "-h"):
//		int help_opt_idx = get_opt(argc, argv, L"-h");
// Returns -1 if the option was not found.
int get_opt(int argc, const wchar_t * argv[], const wchar_t * opt);

// Returns the value of an valued argument like `arg_name=arg_value`.
// Returns NULL if the given argument does not exist.
const wchar_t * get_arg(int argc, const wchar_t * argv[], const wchar_t * arg_name);

// Returns the first index of a "non-option" argument, which is any argument
// that starts with a '-'. Starts searching at `start_idx`, and returns -1
// if no "non-option" arguments remain.
int get_non_opt(int argc, const wchar_t * argv[], int start_idx);

struct convert_result wstr_to_ui(const wchar_t * str);
