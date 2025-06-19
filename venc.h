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
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

// 60F44560-5A20-4857-BFEF-D29773CB8040
DEFINE_GUID(CLSID_NVIDIA_H264_ENCODER,
	0x60F44560, 0x5A20, 0x4857, 0xBF, 0xEF, 0xD2, 0x97, 0x73, 0xCB, 0x80, 0x40);

// 4BE8D3C0-0515-4A37-AD55-E4BAE19AF471
DEFINE_GUID(CLSID_INTEL_QUICKSYNC_H264_ENCODER,
	0x4BE8D3C0, 0x0515, 0x4A37, 0xAD, 0x55, 0xE4, 0xBA, 0xE1, 0x9A, 0xF4, 0x71);

// 556155E0-8B27-44AC-8DBF-2547B9BD7112
DEFINE_GUID(PRIVATE_INTEL_PCI_DEVICE_INSTANCE,
	0x556155E0, 0x8B27, 0x44AC, 0x8D, 0xBF, 0x25, 0x47, 0xB9, 0xBD, 0x71, 0x12);

// 85E4DCCF-F1FE-4117-854D-7CDA2ACC2C77
DEFINE_GUID(MF_MT_D3D_DEVICE,
	0x85E4DCCF, 0xF1FE, 0x4117, 0x85, 0x4D, 0x7C, 0xDA, 0x2A, 0xCC, 0x2C, 0x77);

#define QueryInterface(obj, iid, out)			(obj)->lpVtbl->QueryInterface((obj), (iid), (out))

#define SetGUID(obj, key, value)				(obj)->lpVtbl->SetGUID((obj), (key), (value))
#define SetUINT32(obj, key, value)				(obj)->lpVtbl->SetUINT32((obj), (key), (value))
#define SetUINT64(obj, key, value)				(obj)->lpVtbl->SetUINT64((obj), (key), (value))
#define SetUnknown(obj, key, value)				(obj)->lpVtbl->SetUnknown((obj), (key), (value))
#define SetBlob(obj, key, buf, buf_size)		(obj)->lpVtbl->SetBlob((obj), (key), (buf), (buf_size))
#define GetGUID(obj, key, value_ptr)			(obj)->lpVtbl->GetGUID((obj), (key), (value_ptr))
#define GetUINT32(obj, key, value_ptr)			(obj)->lpVtbl->GetUINT32((obj), (key), (value_ptr))
#define GetBlobSize(obj, key, size_ptr)			(obj)->lpVtbl->GetBlobSize((obj), (key), (size_ptr))
#define GetBlob(obj, key, buf, buf_size)		(obj)->lpVtbl->GetBlob((obj), (key), (buf), (buf_size), NULL)

#ifndef MFSetAttributeSize
inline HRESULT MFSetAttributeSize(IMFAttributes *attrs, REFGUID key, UINT32 width, UINT32 height) {
	ULONGLONG packed = ((ULONGLONG)width << 32) | height;
	return SetUINT64(attrs, key, packed);
}
#endif

#ifndef MFSetAttributeRatio
inline HRESULT MFSetAttributeRatio(IMFAttributes *attrs, REFGUID key, UINT32 num, UINT32 denom) {
	ULONGLONG packed = ((ULONGLONG)num << 32) | denom;
	return SetUINT64(attrs, key, packed);
}
#endif

enum gpu_vendor {
	// Nvidia's H.264 encoder is finnicky and poorly documented. It's probably usable
	// with the Nvidia SDK, but it's not usable directly through MF unless you want
	// to navigate a maze of hidden GUIDs and configuration hell.
	Nvidia = -1000,
	Unknown = 0,
	AMD = 1,
	Intel = 2
};

struct hw_encoder {
	IMFActivate * activate;
	IMFTransform * encoder;
	wchar_t * name;
	enum gpu_vendor vendor;
	UINT32 merit;
	unsigned char status;
};

struct d3d {
	struct hw_encoder * enc;
	IDXGIDevice * dxgi_device;
	IDXGIAdapter * dxgi_adapter;
	ID3D11Device * device;
	ID3D11DeviceContext * context;
	wchar_t adapter_desc[128];
	unsigned char status;
};

struct display {
	struct d3d * d3d;
	IDXGIOutput * output;
	IDXGIOutput1 * output1;
	IDXGIOutputDuplication * dup;
	ID3D11VideoDevice * video_device;
	ID3D11VideoContext * video_context;
	ID3D11VideoProcessorEnumerator * video_processor_enum;
	ID3D11VideoProcessor * video_processor;
	ID3D11Texture2D * frame;
	int width;
	int height;
	unsigned char status;
};

struct mf_state {
	struct d3d * d3d;
	IMFDXGIDeviceManager * device_manager;
	IMFMediaType * out_type;
	IMFMediaType * in_type;
	IMFMediaEventGenerator * event_gen;
	HANDLE h_d3d_device;
	DWORD in_stream_id;
	DWORD out_stream_id;
	UINT reset_token;
	DWORD output_buf_size;
	unsigned char status : 1;
	unsigned char allocates_samples : 1;
};

struct mp4_file {
	const wchar_t * name;
	IMFByteStream * file;
	IMFMediaSink * media_sink;
	IMFStreamSink * sink;
	IMFPresentationClock * clock;
	IMFAsyncCallback * event_callback;
	PROPVARIANT end_of_segment_val;
};

// Initializes Media Foundation
void init_venc();

// Selects a hardware encoder capable of encoding NV12 to H.264. Prioritizes
// based on vendor first, then merit. Returns a zeroed struct if no encoder
// was found.
struct hw_encoder select_encoder();

// Selects a DXGI adapter based on the chosen encoder. It is crucial that the encoder
// and the DXGI device correspond to the same piece of hardware. Returns a zeroed struct
// (except enc) if no device was found.
struct d3d select_dxgi_adapter(struct hw_encoder * enc);

// Selects a D3D display to be recorded. Exits the process if `display_idx` doesn't
// correspond to an actual display.
struct display select_display(struct d3d * d3d, int display_idx);

// Activates the previously selected encoder.
struct mf_state activate_encoder(struct d3d * d3d);

// Sets input and output video types on the encoder.
void prepare_for_streaming(struct display * disp, struct mf_state * mf);

// Creates a `struct mp4_file` with the given name. Does NOT create
// a file or initialize any of the mp4 COM objects.
struct mp4_file prepare_mp4_file(const wchar_t * name);

// Records the selected display for the given duration, at the given `target_fps`.
void capture_screen(
	struct display * disp,
	struct mf_state * mf,
	struct mp4_file * mp4,
	long long duration_s,
	// TODO: Make this configurable
	long long target_fps
);

// These functions release the resources held by each struct, but they do NOT
// try to deallocate the memory occupied by the structs themselves.

void free_hw_encoder(struct hw_encoder * enc);
void free_d3d(struct d3d * d3d);
void free_display(struct display * disp);
void free_mf_state(struct mf_state * mf);
void free_mp4_file(struct mp4_file * mp4);

void print_attrs(IMFAttributes * attrs);
