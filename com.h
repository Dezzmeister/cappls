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
// Declarations for functions to manage COM objects. One of the `acquire_`
// functions should be called whenever a COM object is received, and `release_com_obj`
// should be called to release the COM object when it is no longer needed.
// The `_local` variants accept a `struct com_obj **`; the others work on a global
// list of COM objects owned by the main thread. None of these functions are threadsafe,
// so each thread should own a separate list of COM objects.
// 
// Note: `acquire_` functions don't call AddRef.
#pragma once
#include "lib.h"

#define Release(obj)							(obj)->lpVtbl->Release((obj))

struct com_obj {
	void * obj;
	struct com_obj * next;
	const wchar_t * name;
	// If nonzero, then obj points to an array of IUnknown pointers
	UINT count;
	BOOL is_str;
};

void acquire_com_obj(void * obj, const wchar_t * name);
// Acquires an array of COM objects. NULL elements will be skipped
// when releasing the array. The array can be released by calling
// `release_com_obj`.
void acquire_com_arr(void ** arr, UINT count, const wchar_t * name);
void acquire_com_str(void * str, const wchar_t * name);
void release_com_obj(void * obj);
void drop_com_obj(void * obj);
UINT release_all_com_objs();

void acquire_com_obj_local(struct com_obj ** objs, void * obj, const wchar_t * name);
void acquire_com_arr_local(struct com_obj ** objs, void ** arr, UINT count, const wchar_t * name);
void acquire_com_str_local(struct com_obj ** objs, void * str, const wchar_t * name);
void release_com_obj_local(struct com_obj ** objs, void * obj);
void drop_com_obj_local(struct com_obj ** objs, void * obj);
UINT release_all_com_objs_local(struct com_obj ** objs);
