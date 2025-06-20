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
#include "com.h"

static struct com_obj * held_objs = NULL;

UINT release_com_node(struct com_obj * node, const wchar_t * context);

void acquire_com_obj(void * obj, const wchar_t * const name) {
	acquire_com_obj_local(&held_objs, obj, name);
}

void acquire_com_arr(void ** arr, UINT count, const wchar_t * const name) {
	acquire_com_arr_local(&held_objs, arr, count, name);
}

void release_com_obj(void * obj) {
	release_com_obj_local(&held_objs, obj);
}

void drop_com_obj(void * obj) {
	drop_com_obj_local(&held_objs, obj);
}

UINT release_all_com_objs() {
	return release_all_com_objs_local(&held_objs);
}

void acquire_com_obj_local(struct com_obj ** objs, void * obj, const wchar_t * name) {
	struct com_obj * next = (struct com_obj *)alloc_or_die(sizeof(struct com_obj));
	next->obj = obj;
	next->next = *objs;
	next->name = name;
	next->count = 0;
	*objs = next;
}

void acquire_com_arr_local(struct com_obj ** objs, void ** obj, UINT count, const wchar_t * name) {
	struct com_obj * next = (struct com_obj *)alloc_or_die(sizeof(struct com_obj));
	next->obj = obj;
	next->next = *objs;
	next->name = name;
	next->count = count;
	*objs = next;
}

void release_com_obj_local(struct com_obj ** objs, void * obj) {
	struct com_obj * prev = NULL;
	struct com_obj * curr = *objs;

	while (curr) {
		if (curr->obj == obj) {
			release_com_node(curr, L"one");

			if (prev) {
				prev->next = curr->next;
			}

			if (*objs == curr) {
				*objs = curr->next;
			}

			dealloc(curr);
			return;
		}

		prev = curr;
		curr = curr->next;
	}

	print_err_fmt(L"Attempted to release COM object that was not present in COM object list\n");
}

void drop_com_obj_local(struct com_obj ** objs, void * obj) {
	struct com_obj * prev = NULL;
	struct com_obj * curr = *objs;

	while (curr) {
		if (curr->obj == obj) {
			if (prev) {
				prev->next = curr->next;
			}

			if (*objs == curr) {
				*objs = curr->next;
			}

			dealloc(curr);
			return;
		}

		prev = curr;
		curr = curr->next;
	}

	print_err_fmt(L"Attempted to drop COM object that was not present in COM object list\n");
}

UINT release_all_com_objs_local(struct com_obj ** objs) {
	struct com_obj * curr = *objs;
	UINT i = 0;

	while (curr) {
		struct com_obj * next = curr->next;

		i += release_com_node(curr, L"all");
		dealloc(curr);

		curr = next;
	}

	return i;
}

UINT release_com_node(struct com_obj * node, const wchar_t * context) {
	UINT num_freed = 1;

	// TODO: Log levels
	// print_fmt(L"(Context: %1!s!): Releasing %2!s!\n", context, node->name);

	if (node->count == 0) {
		Release((IUnknown *)node->obj);
	} else {
		IUnknown ** arr = (IUnknown **)node->obj;

		for (UINT i = 0; i < node->count; i++) {
			if (arr[i]) {
				Release(arr[i]);
				num_freed++;
			}
		}

		CoTaskMemFree(node->obj);
	}

	return num_freed;
}
