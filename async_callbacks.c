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
#include "async_callbacks.h"
#include "com.h"
#include "venc.h"
#include "logging.h"
#include <Mferror.h>
#include <propvarutil.h>

HRESULT __stdcall common_QueryInterface(
	IMFAsyncCallback * this,
	REFIID riid,
	void ** ppv_obj
);
ULONG __stdcall refcounted_AddRef(IMFAsyncCallback * this);
ULONG __stdcall refcounted_Release(IMFAsyncCallback * this);
HRESULT __stdcall common_GetParameters(
	IMFAsyncCallback * this,
	DWORD * pdw_flags,
	DWORD * pdw_queue
);

HRESULT __stdcall mp4_event_callback_Invoke(IMFAsyncCallback * this, IMFAsyncResult * result);
HRESULT __stdcall mp4_finalize_callback_Invoke(IMFAsyncCallback * this, IMFAsyncResult * result);

enum semaphore_status wait_for_semaphore(HANDLE semaphore, DWORD timeout_ms);

IMFAsyncCallbackVtbl mp4_event_callback_vtbl = {
	.AddRef = refcounted_AddRef,
	.GetParameters = common_GetParameters,
	.Invoke = mp4_event_callback_Invoke,
	.QueryInterface = common_QueryInterface,
	.Release = refcounted_Release
};

IMFAsyncCallbackVtbl mp4_finalize_callback_vtbl = {
	.AddRef = refcounted_AddRef,
	.GetParameters = common_GetParameters,
	.Invoke = mp4_finalize_callback_Invoke,
	.QueryInterface = common_QueryInterface,
	.Release = refcounted_Release
};

struct mp4_event_callback * mp4_event_callback_new(IMFMediaEventGenerator * event_gen) {
	struct mp4_event_callback * this = (struct mp4_event_callback *)alloc_or_die(sizeof(struct mp4_event_callback));

	this->base.lpVtbl = &mp4_event_callback_vtbl;
	this->base.held_objs = NULL;
	this->base.ref_count = 0;
	this->event_gen = event_gen;
	this->end_of_segment_semaphore = CreateSemaphore(
		NULL,
		0,
		1,
		NULL
	);
	check_err(this->end_of_segment_semaphore == NULL);

	event_gen->lpVtbl->AddRef(event_gen);
	acquire_com_obj_local(&this->base.held_objs, event_gen, L"event_gen");

	this->base.lpVtbl->AddRef((IMFAsyncCallback *)this);

	return this;
}

HRESULT __stdcall mp4_event_callback_Invoke(
	IMFAsyncCallback * super,
	IMFAsyncResult * result
) {
	static wchar_t buf[4096];

	struct mp4_event_callback * this = (struct mp4_event_callback *)super;

	IMFMediaEventGenerator * event_gen = this->event_gen;
	IMFMediaEvent * event;

	if (! event_gen) {
		return S_OK;
	}

	HRESULT hr = event_gen->lpVtbl->EndGetEvent(event_gen, result, &event);

	if (hr == MF_E_SHUTDOWN) {
		return S_OK;
	}
	check_hresult(hr, L"Failed to finish getting mp4 sink event");
	acquire_com_obj_local(&this->base.held_objs, event, L"event");

	MediaEventType type;
	HRESULT status;
	PROPVARIANT val;

	hr = event->lpVtbl->GetType(event, &type);
	check_hresult(hr, L"Failed to get event type");

	hr = event->lpVtbl->GetStatus(event, &status);
	check_hresult(hr, L"Failed to get event status");

	hr = event->lpVtbl->GetValue(event, &val);
	check_hresult(hr, L"Failed to get event value");

	if (log_level >= Debug) {
		hr = PropVariantToString(&val, buf, ARR_SIZE(buf));

		if (hr == TYPE_E_ELEMENTNOTFOUND) {
			copy_wstr(buf, L"???");
		} else if (hr == TYPE_E_TYPEMISMATCH) {
			copy_wstr(buf, L"!!!");
		} else {
			check_hresult(hr, L"Failed to convert value to string");
		}

		if (val.vt == VT_CLSID) {
			const wchar_t * val_name = get_guid_name(val.puuid);

			if (val_name) {
				copy_wstr(buf, val_name);
			}
		}

		log_debug(
			L"(mp4 Sink Event): [type: %1!d!] [status: 0x%2!x!] [value: %3!s!]\n",
			type,
			status,
			buf
		);
	}

	if (type == MEStreamSinkMarker && val.vt == VT_UI4 && val.ulVal == MFSTREAMSINK_MARKER_ENDOFSEGMENT) {
		BOOL signal_status = ReleaseSemaphore(
			this->end_of_segment_semaphore,
			1,
			NULL
		);
		check_err(! signal_status);
	}

	PropVariantClear(&val);

	release_com_obj_local(&this->base.held_objs, event);

	if (type != MEStreamSinkStopped) {
		hr = event_gen->lpVtbl->BeginGetEvent(event_gen, super, NULL);
	}

	return hr;
}

enum semaphore_status wait_for_end_of_segment(struct mp4_event_callback * callback, DWORD timeout_ms) {
	return wait_for_semaphore(callback->end_of_segment_semaphore, timeout_ms);
}

struct mp4_finalize_callback * mp4_finalize_callback_new(IMFFinalizableMediaSink * media_sink) {
	struct mp4_finalize_callback * this = (struct mp4_finalize_callback *)alloc_or_die(sizeof(struct mp4_finalize_callback));

	this->base.lpVtbl = &mp4_finalize_callback_vtbl;
	this->base.held_objs = NULL;
	this->base.ref_count = 0;
	this->media_sink = media_sink;
	this->done_semaphore = CreateSemaphore(
		NULL,
		0,
		1,
		NULL
	);
	check_err(this->done_semaphore == NULL);

	media_sink->lpVtbl->AddRef(media_sink);
	acquire_com_obj_local(&this->base.held_objs, media_sink, L"media_sink");

	this->base.lpVtbl->AddRef((IMFAsyncCallback *)this);

	return this;
}

HRESULT __stdcall mp4_finalize_callback_Invoke(IMFAsyncCallback * super, IMFAsyncResult * result) {
	struct mp4_finalize_callback * this = (struct mp4_finalize_callback *)super;

	IMFFinalizableMediaSink * media_sink = this->media_sink;

	if (! media_sink) {
		return S_OK;
	}

	HRESULT hr = media_sink->lpVtbl->EndFinalize(media_sink, result);
	check_hresult(hr, L"Failed to end mp4 finalization");

	BOOL signal_status = ReleaseSemaphore(
		this->done_semaphore,
		1,
		NULL
	);
	check_err(! signal_status);

	return S_OK;
}

void start_finalization(struct mp4_finalize_callback * callback) {
	HRESULT hr = callback->media_sink->lpVtbl->BeginFinalize(callback->media_sink, (IMFAsyncCallback *)callback, NULL);
	check_hresult(hr, L"Failed to start finalizing mp4");
}

enum semaphore_status wait_for_finalization(struct mp4_finalize_callback * callback, DWORD timeout_ms) {
	return wait_for_semaphore(callback->done_semaphore, timeout_ms);
}

HRESULT __stdcall common_QueryInterface(
	IMFAsyncCallback * this,
	REFIID riid,
	void ** ppv_obj
) {
	if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFAsyncCallback)) {
		*ppv_obj = this;
		this->lpVtbl->AddRef(this);

		return S_OK;
	}

	*ppv_obj = NULL;
	return E_NOINTERFACE;
}

ULONG __stdcall refcounted_AddRef(IMFAsyncCallback * super) {
	struct refcounted * this = (struct refcounted *)super;

	return InterlockedIncrement(&this->ref_count);
}

ULONG __stdcall refcounted_Release(IMFAsyncCallback * super) {
	struct refcounted * this = (struct refcounted *)super;

	LONG ref = InterlockedDecrement(&this->ref_count);

	if (ref == 0) {
		UINT released_objs_count = release_all_com_objs_local(&this->held_objs);
		log_verbose(L"(Async callback) Released %1!d! COM object(s)\n", released_objs_count);

		dealloc(this);
	}

	return ref;
}

HRESULT __stdcall common_GetParameters(
	IMFAsyncCallback * this,
	DWORD * pdw_flags,
	DWORD * pdw_queue
) {
	*pdw_flags = 0;
	*pdw_queue = MFASYNC_CALLBACK_QUEUE_STANDARD;

	return E_NOTIMPL;
}

enum semaphore_status wait_for_semaphore(HANDLE semaphore, DWORD timeout_ms) {
	DWORD status = WaitForSingleObject(
		semaphore,
		timeout_ms
	);

	if (status == WAIT_OBJECT_0) {
		return Done;
	} else if (status == WAIT_TIMEOUT) {
		return Timeout;
	} else if (status == WAIT_ABANDONED) {
		return Interrupted;
	}

	check_err(status == WAIT_FAILED);
	return Interrupted;
}
