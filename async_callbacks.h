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
#include "com.h"
#include <mfobjects.h>

enum semaphore_status {
	Done,
	Timeout,
	Interrupted
};

struct refcounted {
	IMFAsyncCallbackVtbl * lpVtbl;
	struct com_obj * held_objs;
	LONG ref_count;
};

struct mp4_event_callback {
	struct refcounted base;
	IMFMediaEventGenerator * event_gen;
	HANDLE end_of_segment_semaphore;
};

struct mp4_event_callback * mp4_event_callback_new(IMFMediaEventGenerator * event_gen);
enum semaphore_status wait_for_end_of_segment(struct mp4_event_callback * callback, DWORD timeout_ms);

struct mp4_finalize_callback {
	struct refcounted base;
	IMFFinalizableMediaSink * media_sink;
	HANDLE done_semaphore;
};

struct mp4_finalize_callback * mp4_finalize_callback_new(IMFFinalizableMediaSink * media_sink);
void start_finalization(struct mp4_finalize_callback * callback);
enum semaphore_status wait_for_finalization(struct mp4_finalize_callback * callback, DWORD timeout_ms);
