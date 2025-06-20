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
#define UNICODE
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define INITGUID
#include <Windows.h>
#include <shellapi.h>
#include <initguid.h>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>

#define ARR_SIZE(T)				(sizeof(T) / sizeof((T)[0]))

// The application's "fake" entry point. The true entry point will set up
// stdout, stderr, a heap, and parse command line args before calling
// wmain. wmain should exit by returning or calling exit_process. If wmain
// returns, the true entry point will call exit_process.
int wmain(DWORD argc, LPCWSTR argv[]);

// Will be called by the true entry point when wmain returns. Useful for doing
// global cleanup (like releasing COM objects).
void exit_process(UINT code);

void print_fmt(LPCWSTR fmt_str, ...);
void print_err_fmt(LPCWSTR fmt_str, ...);
DWORD print_str_fmt(LPWSTR out, DWORD size, LPCWSTR fmt_str, ...);
void check_err(BOOL cond);
void check_hresult(HRESULT code, LPCWSTR err_prefix);
void * alloc_or_die(SIZE_T num_bytes);
void dealloc(void * heap_mem);

void * memcpy(void * dest, const void * src, size_t count);
void * memset(void * dest, int c, size_t count);
int memcmp(const void * ptr1, const void * ptr2, size_t num);

int find_wstr(const wchar_t * str, const wchar_t * substr);
wchar_t * copy_wstr(wchar_t * dest, const wchar_t * src);
int wstr_len(const wchar_t * str);
BOOL wstr_eq(const wchar_t * str1, const wchar_t * str2);

const wchar_t * basename(const wchar_t * path);

const wchar_t * get_guid_name(const GUID * guid);
