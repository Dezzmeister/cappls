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
#include "venc.h"
#include "com.h"
#include "async_callbacks.h"
#include <propvarutil.h>
#include <codecapi.h>
#include <Mferror.h>

struct in_out_stream_ids {
	DWORD in_stream_id;
	DWORD out_stream_id;
};

static D3D_FEATURE_LEVEL feature_levels[] = {
	D3D_FEATURE_LEVEL_11_1,
	D3D_FEATURE_LEVEL_11_0,
};

void init_venc() {
	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	check_hresult(hr, L"Failed to start Media Foundation\n");
	print_fmt(L"Initialized Media Foundation\n");
}

struct hw_encoder select_encoder() {
	MFT_REGISTER_TYPE_INFO input_type = {
		.guidMajorType = MFMediaType_Video,
		.guidSubtype = MFVideoFormat_NV12
	};

	MFT_REGISTER_TYPE_INFO output_type = {
		.guidMajorType = MFMediaType_Video,
		.guidSubtype = MFVideoFormat_H264
	};

	IMFActivate ** activate_arr;
	UINT32 count = 0;
	HRESULT hr = MFTEnumEx(
		MFT_CATEGORY_VIDEO_ENCODER,
		MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
		&input_type,
		&output_type,
		&activate_arr,
		&count
	);
	check_hresult(hr, L"Failed to enumerate encoders");
	acquire_com_arr(activate_arr, count, L"activate_arr");

	UINT best_encoder_idx = 0;
	struct hw_encoder best = { 0 };

	for (unsigned int i = 0; i < count; i++) {
		IMFActivate * activate = activate_arr[i];
		GUID clsid;
		wchar_t * name;
		UINT32 name_len;
		UINT32 merit;

		hr |= GetGUID(activate, &MFT_TRANSFORM_CLSID_Attribute, &clsid);
		hr |= activate->lpVtbl->GetAllocatedString(activate, &MFT_FRIENDLY_NAME_Attribute, &name, &name_len);
		hr |= GetUINT32(activate, &MFT_CODEC_MERIT_Attribute, &merit);

		if (! SUCCEEDED(hr)) {
			hr = S_OK;
			continue;
		}

		enum gpu_vendor vendor = Unknown;

		if (find_str(name, L"Intel") != -1) {
			vendor = Intel;
			// TODO: Test this
		} else if (find_str(name, L"AMD") != -1) {
			vendor = AMD;
		} else if (find_str(name, L"NVIDIA") != -1) {
			vendor = Nvidia;
		}

		if (vendor > best.vendor || (vendor == best.vendor && merit > best.merit)) {
			best_encoder_idx = i;
			best.activate = activate;
			best.vendor = vendor;
			best.merit = merit;

			if (best.name) {
				CoTaskMemFree(best.name);
			}

			best.name = name;
		} else {
			CoTaskMemFree(name);
		}
	}

	if (best.activate) {
		activate_arr[best_encoder_idx] = NULL;
		acquire_com_obj(best.activate, L"best.activate");
	}

	release_com_obj(activate_arr);

	best.status = 1;

	return best;
}

struct d3d select_dxgi_adapter(struct hw_encoder * enc) {
	struct d3d d3d = {
		.enc = enc
	};

	IDXGIFactory1 * factory;
	HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
	check_hresult(hr, L"Failed to create DXGI factory");
	acquire_com_obj(factory, L"factory");

	UINT i = 0;
	IDXGIAdapter1 * adapter1;

	while (1) {
		hr = factory->lpVtbl->EnumAdapters1(factory, i, &adapter1);

		if (hr == DXGI_ERROR_NOT_FOUND) {
			return d3d;
		}
		check_hresult(hr, L"Failed to enumerate DXGI adapters");
		acquire_com_obj(adapter1, L"adapter");

		DXGI_ADAPTER_DESC1 desc;
		hr = adapter1->lpVtbl->GetDesc1(adapter1, &desc);
		check_hresult(hr, L"Failed to get adapter description");

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			release_com_obj(adapter1);
			continue;
		}

		enum gpu_vendor vendor = Unknown;

		switch (desc.VendorId) {
			case 0x10DE: {
				vendor = Nvidia;
				break;
			}
			case 0x1002:
			case 0x1022: {
				vendor = AMD;
				break;
			}
			case 0x8086:
			case 0x8087:
			case 0x163C: {
				vendor = Intel;
				break;
			}
		}

		if (vendor == Unknown) {
			if (find_str(desc.Description, L"Intel") != -1) {
				vendor = Intel;
				// TODO: Test this
			} else if (find_str(desc.Description, L"AMD") != -1) {
				vendor = AMD;
			} else if (find_str(desc.Description, L"NVIDIA") != -1) {
				vendor = Nvidia;
			}
		}

		if (vendor == Unknown) {
			print_err_fmt(L"Unknown DXGI adapter vendor (0x%1!x!): %2!s!\n", desc.VendorId, desc.Description);
		} else if (vendor == enc->vendor) {
			d3d.dxgi_adapter = (IDXGIAdapter *)adapter1;
			copy_wstr(d3d.adapter_desc, desc.Description);
			break;
		}

		i++;
		release_com_obj(adapter1);
	}

	release_com_obj(factory);

	if (! d3d.dxgi_adapter) {
		return d3d;
	}

	hr = D3D11CreateDevice(
		d3d.dxgi_adapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		NULL,
		D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		feature_levels,
		ARR_SIZE(feature_levels),
		D3D11_SDK_VERSION,
		&d3d.device,
		NULL,
		&d3d.context
	);
	check_hresult(hr, L"Failed to create D3D11 device");
	acquire_com_obj(d3d.device, L"d3d.device");
	acquire_com_obj(d3d.context, L"d3d.context");

	hr = QueryInterface(d3d.device, &IID_IDXGIDevice, (void **)&d3d.dxgi_device);
	check_hresult(hr, L"Failed to get IDXGIDevice");
	acquire_com_obj(d3d.dxgi_device, L"d3d.dxgi_device");

	d3d.status = 1;

	return d3d;
}

struct display select_display(struct d3d * d3d, int display_idx) {
	struct display disp = {
		.d3d = d3d
	};

	UINT i = 0;

	while (1) {
		IDXGIOutput * output;
		HRESULT hr = d3d->dxgi_adapter->lpVtbl->EnumOutputs(d3d->dxgi_adapter, i, &output);

		if (hr == DXGI_ERROR_NOT_FOUND) {
			break;
		}
		check_hresult(hr, L"Failed to enumerate DXGI outputs");
		acquire_com_obj(output, L"output");

		if (i == display_idx) {
			disp.output = output;
			break;
		}

		release_com_obj(output);
	}

	if (! disp.output) {
		return disp;
	}

	HRESULT hr = QueryInterface(disp.output, &IID_IDXGIOutput1, &disp.output1);
	check_hresult(hr, L"Failed to get output1");
	acquire_com_obj(disp.output1, L"disp.output1");

	hr = disp.output1->lpVtbl->DuplicateOutput(disp.output1, (IUnknown *)d3d->device, &disp.dup);
	check_hresult(hr, L"Failed to create output duplication");
	acquire_com_obj(disp.dup, L"disp.dup");

	hr = QueryInterface(d3d->device, &IID_ID3D11VideoDevice, (void **)&disp.video_device);
	check_hresult(hr, L"Failed to get ID3D11VideoDevice");
	acquire_com_obj(disp.video_device, L"disp.video_device");

	hr = QueryInterface(d3d->context, &IID_ID3D11VideoContext, (void **)&disp.video_context);
	check_hresult(hr, L"Failed to get ID3D11VideoContext");
	acquire_com_obj(disp.video_context, L"disp.video_context");

	DXGI_OUTPUT_DESC display_desc;
	hr = disp.output->lpVtbl->GetDesc(disp.output, &display_desc);
	check_hresult(hr, L"Failed to get display description");

	disp.width = display_desc.DesktopCoordinates.right - display_desc.DesktopCoordinates.left;
	disp.height = display_desc.DesktopCoordinates.bottom - display_desc.DesktopCoordinates.top;

	D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {
		.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
		.InputWidth = disp.width,
		.InputHeight = disp.height,
		.OutputWidth = disp.width,
		.OutputHeight = disp.height,
		.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL
	};

	hr = disp.video_device->lpVtbl->CreateVideoProcessorEnumerator(disp.video_device, &desc, &disp.video_processor_enum);
	check_hresult(hr, L"Failed to create video processor enumerator");
	acquire_com_obj(disp.video_processor_enum, L"disp.video_processor_enum");

	hr = disp.video_device->lpVtbl->CreateVideoProcessor(disp.video_device, disp.video_processor_enum, 0, &disp.video_processor);
	check_hresult(hr, L"Failed to create video processor");
	acquire_com_obj(disp.video_processor, L"disp.video_processor");

	disp.status = 1;

	return disp;
}

struct in_out_stream_ids select_streams(IMFTransform * enc) {
	struct in_out_stream_ids ids = { 0 };

	DWORD in_stream_count;
	DWORD out_stream_count;

	HRESULT hr = enc->lpVtbl->GetStreamCount(enc, &in_stream_count, &out_stream_count);
	check_hresult(hr, L"Failed to get encoder stream counts");

	DWORD * in_stream_ids = (DWORD *)alloc_or_die(in_stream_count * sizeof(DWORD));
	DWORD * out_stream_ids = (DWORD *)alloc_or_die(out_stream_count * sizeof(DWORD));

	hr = enc->lpVtbl->GetStreamIDs(enc, in_stream_count, in_stream_ids, out_stream_count, out_stream_ids);
	if (hr == E_NOTIMPL) {
		ids.in_stream_id = 0;
		ids.out_stream_id = 0;
	} else {
		check_hresult(hr, L"Failed to get encoder stream IDs");
		// TODO: Deal with encoders that don't have any streams until AddStream is called
		ids.in_stream_id = in_stream_ids[0];
		ids.out_stream_id = out_stream_ids[0];
	}

	dealloc(out_stream_ids);
	dealloc(in_stream_ids);

	return ids;
}

struct mf_state activate_encoder(struct d3d * d3d) {
	struct mf_state mf = {
		.d3d = d3d,
		.h_d3d_device = INVALID_HANDLE_VALUE
	};

	HRESULT hr = MFCreateDXGIDeviceManager(&mf.reset_token, &mf.device_manager);
	check_hresult(hr, L"Failed to create DXGI device manager");
	acquire_com_obj(mf.device_manager, L"disp.device_manager");

	hr = mf.device_manager->lpVtbl->ResetDevice(mf.device_manager, (IUnknown *)d3d->device, mf.reset_token);
	check_hresult(hr, L"Failed to associate DXGI device manager with D3D11 device");
	drop_com_obj(d3d->device);

	hr = mf.device_manager->lpVtbl->OpenDeviceHandle(mf.device_manager, &mf.h_d3d_device);
	check_hresult(hr, L"Failed to reopen D3D device");

	hr = mf.device_manager->lpVtbl->GetVideoService(
		mf.device_manager,
		mf.h_d3d_device,
		&IID_ID3D11Device,
		&d3d->device
	);
	check_hresult(hr, L"Failed to get fresh D3D service");
	acquire_com_obj(d3d->device, L"d3d->device");

	struct hw_encoder * enc = d3d->enc;

	hr = SetUINT32(enc->activate, &MF_SA_D3D11_AWARE, TRUE);
	hr |= SetUINT32(enc->activate, &MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT);
	hr |= SetUINT32(enc->activate, &MF_SA_D3D11_BINDFLAGS, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_VIDEO_ENCODER);
	check_hresult(hr, L"Failed to set encoder activate attributes");

	hr = enc->activate->lpVtbl->ActivateObject(enc->activate, &IID_IMFTransform, (void **)&enc->encoder);
	check_hresult(hr, L"Failed to activate encoder");
	acquire_com_obj(enc->encoder, L"enc->encoder");
	release_com_obj(enc->activate);
	enc->activate = NULL;

	IMFAttributes * encoder_attrs;
	hr = enc->encoder->lpVtbl->GetAttributes(enc->encoder, &encoder_attrs);
	check_hresult(hr, L"Failed to get encoder attributes");
	acquire_com_obj(encoder_attrs, L"encoder_attrs");

	hr = SetUINT32(encoder_attrs, &MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
	check_hresult(hr, L"Failed to unlock async encoder");

	release_com_obj(encoder_attrs);

	hr = enc->encoder->lpVtbl->ProcessMessage(enc->encoder, MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)mf.device_manager);
	check_hresult(hr, L"Failed to set D3D manager on encoder");

	struct in_out_stream_ids stream_ids = select_streams(enc->encoder);

	mf.in_stream_id = stream_ids.in_stream_id;
	mf.out_stream_id = stream_ids.out_stream_id;

	mf.status = 1;

	return mf;
}

void prepare_for_streaming(struct display * disp, struct mf_state * mf) {
	HRESULT hr = MFCreateMediaType(&mf->out_type);
	check_hresult(hr, L"Failed to create video disp type");
	acquire_com_obj(mf->out_type, L"mf->out_type");

	hr = SetGUID(mf->out_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
	hr |= SetGUID(mf->out_type, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
	hr |= SetUINT32(mf->out_type, &MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
	hr |= SetUINT32(mf->out_type, &MF_MT_AVG_BITRATE, 10000000);
	hr |= SetUINT32(mf->out_type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	hr |= MFSetAttributeSize((IMFAttributes *)mf->out_type, &MF_MT_FRAME_SIZE, disp->width, disp->height);
	hr |= MFSetAttributeRatio((IMFAttributes *)mf->out_type, &MF_MT_FRAME_RATE, 30, 1);
	hr |= MFSetAttributeRatio((IMFAttributes *)mf->out_type, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	check_hresult(hr, L"Failed to set attributes for output type");

	struct hw_encoder * enc = mf->d3d->enc;

	hr = enc->encoder->lpVtbl->SetOutputType(enc->encoder, mf->out_stream_id, mf->out_type, 0);
	check_hresult(hr, L"Failed to set output type");

	hr = MFCreateMediaType(&mf->in_type);
	check_hresult(hr, L"Failed to create input type");
	acquire_com_obj(mf->in_type, L"mf->in_type");

	hr = SetGUID(mf->in_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
	hr |= SetGUID(mf->in_type, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
	hr |= SetUINT32(mf->in_type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	hr |= MFSetAttributeSize((IMFAttributes *)mf->in_type, &MF_MT_FRAME_SIZE, disp->width, disp->height);
	hr |= MFSetAttributeRatio((IMFAttributes *)mf->in_type, &MF_MT_FRAME_RATE, 30, 1);
	hr |= MFSetAttributeRatio((IMFAttributes *)mf->in_type, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	check_hresult(hr, L"Failed to set attributes for input type");

	hr = enc->encoder->lpVtbl->SetInputType(enc->encoder, mf->in_stream_id, mf->in_type, 0);
	check_hresult(hr, L"Failed to set input type");

	hr = QueryInterface(enc->encoder, &IID_IMFMediaEventGenerator, (void **)&mf->event_gen);
	check_hresult(hr, L"Failed to get MFT event generator");
	acquire_com_obj(mf->event_gen, L"event_gen");

	MFT_OUTPUT_STREAM_INFO out_stream_info;
	hr = enc->encoder->lpVtbl->GetOutputStreamInfo(enc->encoder, mf->out_stream_id, &out_stream_info);
	check_hresult(hr, L"Failed to get encoder output stream info");

	mf->allocates_samples = !!(out_stream_info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES));
	mf->output_buf_size = out_stream_info.cbSize;
}

void create_mp4_sink(struct mf_state * mf, struct mp4_file * mp4) {
	if (mp4->sink) {
		release_com_obj(mp4->sink);
		mp4->sink = NULL;
	}

	if (mp4->media_sink) {
		print_err_fmt(L"Tried to create media sink twice");
		exit_process(1);
	}

	HRESULT hr = MFCreateFile(
		MF_ACCESSMODE_WRITE,
		MF_OPENMODE_DELETE_IF_EXIST,
		MF_FILEFLAGS_NONE,
		mp4->name,
		&mp4->file
	);
	check_hresult(hr, L"Failed to create mp4 file");
	acquire_com_obj(mp4->file, L"mp4->file");

	hr = MFCreateMPEG4MediaSink(
		mp4->file,
		mf->out_type,
		NULL,
		&mp4->media_sink
	);
	check_hresult(hr, L"Failed to create mp4 media sink");
	acquire_com_obj(mp4->media_sink, L"mp4->media_sink");

	DWORD sink_flags;
	mp4->media_sink->lpVtbl->GetCharacteristics(mp4->media_sink, &sink_flags);

	if (! (sink_flags & MEDIASINK_RATELESS)) {
		// TODO: Support media sinks that use presentation clocks for more
		// than internal event timing
		print_err_fmt(L"Only rateless media sinks are supported");
		exit_process(1);
	}

	hr = mp4->media_sink->lpVtbl->GetStreamSinkByIndex(mp4->media_sink, 0, &mp4->sink);
	check_hresult(hr, L"Failed to get stream sink");
	acquire_com_obj(mp4->sink, L"mp4->sink");

	IMFMediaTypeHandler * media_type_handler;
	hr = mp4->sink->lpVtbl->GetMediaTypeHandler(mp4->sink, &media_type_handler);
	check_hresult(hr, L"Failed to get mp4 sink media type handler");
	acquire_com_obj(media_type_handler, L"media_type_handler");

	hr = media_type_handler->lpVtbl->SetCurrentMediaType(media_type_handler, mf->out_type);
	check_hresult(hr, L"Failed to set mp4 media type");

	release_com_obj(media_type_handler);

	print_attrs((IMFAttributes *)mf->out_type);

	hr = MFCreatePresentationClock(&mp4->clock);
	check_hresult(hr, L"Failed to create presentation clock");
	acquire_com_obj(mp4->clock, L"mp4->clock");

	IMFPresentationTimeSource * time_source;
	hr = MFCreateSystemTimeSource(&time_source);
	check_hresult(hr, L"Failed to create system time source");
	acquire_com_obj(time_source, L"time_source");

	hr = mp4->clock->lpVtbl->SetTimeSource(mp4->clock, time_source);
	check_hresult(hr, L"Failed to set clock time source");
	release_com_obj(time_source);

	hr = mp4->media_sink->lpVtbl->SetPresentationClock(mp4->media_sink, mp4->clock);
	check_hresult(hr, L"Failed to set presentation clock on media sink");

	IMFClockStateSink * clock_state;
	hr = QueryInterface(mp4->media_sink, &IID_IMFClockStateSink, (void **)&clock_state);
	check_hresult(hr, L"Failed to get sink clock state");
	acquire_com_obj(clock_state, L"clock_state");

	hr = clock_state->lpVtbl->OnClockStart(clock_state, 0, 0);
	check_hresult(hr, L"Failed to start clock");
	release_com_obj(clock_state);

	hr = mp4->sink->lpVtbl->Flush(mp4->sink);
	check_hresult(hr, L"Failed to flush mp4 sink");

	hr = mp4->clock->lpVtbl->Start(mp4->clock, 0);
	check_hresult(hr, L"Failed to start clock");

	IMFMediaEventGenerator * event_gen = (IMFMediaEventGenerator *)mp4->sink;

	mp4->event_callback = (IMFAsyncCallback *)mp4_event_callback_new(event_gen);
	acquire_com_obj(mp4->event_callback, L"mp4->event_callback");
	hr = mp4->sink->lpVtbl->BeginGetEvent(mp4->sink, mp4->event_callback, NULL);
	check_hresult(hr, L"Failed to start getting mp4 sink events");
}

struct mp4_file prepare_mp4_file(const wchar_t * name) {
	struct mp4_file mp4 = {
		.name = name
	};

	PropVariantInit(&mp4.end_of_segment_val);
	mp4.end_of_segment_val.vt = VT_UI4;
	mp4.end_of_segment_val.ulVal = MFSTREAMSINK_MARKER_ENDOFSEGMENT;

	return mp4;
}

static void capture_frame(struct display * disp) {
	DXGI_OUTDUPL_FRAME_INFO frame_info;
	IDXGIResource * desktop_resource;

	HRESULT hr = disp->dup->lpVtbl->AcquireNextFrame(disp->dup, 1000, &frame_info, &desktop_resource);
	check_hresult(hr, L"Failed to acquire next frame");
	acquire_com_obj(desktop_resource, L"desktop_resource");

	hr = QueryInterface(desktop_resource, &IID_ID3D11Texture2D, (void **)&disp->frame);
	check_hresult(hr, L"Failed to get frame as texture");
	acquire_com_obj(disp->frame, L"frame");
	release_com_obj(desktop_resource);
}

static void release_frame(struct display * disp) {
	release_com_obj(disp->frame);
	disp->dup->lpVtbl->ReleaseFrame(disp->dup);
	disp->frame = NULL;
}

static IMFSample * capture_video_frame(
	struct display * disp,
	struct mf_state * mf,
	LONGLONG time,
	LONGLONG duration
) {
	const struct d3d * d3d = mf->d3d;

	capture_frame(disp);

	D3D11_TEXTURE2D_DESC frame_desc;
	disp->frame->lpVtbl->GetDesc(disp->frame, &frame_desc);

	D3D11_TEXTURE2D_DESC nv12_desc = {
		.Width = frame_desc.Width,
		.Height = frame_desc.Height,
		.MipLevels = 1,
		.ArraySize = 1,
		.Usage = D3D11_USAGE_DEFAULT,
		.SampleDesc = {
			.Count = 1,
		},
		.Format = DXGI_FORMAT_NV12,
		.CPUAccessFlags = 0,
		.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
		.MiscFlags = 0
	};

	ID3D11Texture2D * nv12_tex;
	HRESULT hr = d3d->device->lpVtbl->CreateTexture2D(d3d->device, &nv12_desc, NULL, &nv12_tex);
	check_hresult(hr, L"Failed to create NV12 texture");
	acquire_com_obj(nv12_tex, L"nv12_tex");

	IDXGISurface * nv12_dxgi_surface;
	hr = QueryInterface(nv12_tex, &IID_IDXGISurface, (void **)&nv12_dxgi_surface);
	check_hresult(hr, L"Failed to get IDXGISurface from NV12 texture");
	acquire_com_obj(nv12_dxgi_surface, L"nv12_dxgi_surface");
	release_com_obj(nv12_tex);

	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_view_desc = {
		.FourCC = 0,
		.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D
	};

	ID3D11VideoProcessorInputView * input_view;
	hr = disp->video_device->lpVtbl->CreateVideoProcessorInputView(
		disp->video_device,
		(ID3D11Resource *)disp->frame,
		disp->video_processor_enum,
		&input_view_desc,
		&input_view
	);
	check_hresult(hr, L"Failed to create video processor input view");
	acquire_com_obj(input_view, L"input_view");

	D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_view_desc = {
		.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D
	};

	ID3D11VideoProcessorOutputView * output_view;
	hr = disp->video_device->lpVtbl->CreateVideoProcessorOutputView(
		disp->video_device,
		(ID3D11Resource *)nv12_tex,
		disp->video_processor_enum,
		&output_view_desc,
		&output_view
	);
	check_hresult(hr, L"Failed to create video processor output view");
	acquire_com_obj(output_view, L"output_view");

	D3D11_VIDEO_PROCESSOR_STREAM stream = {
		.Enable = TRUE,
		.pInputSurface = input_view
	};

	hr = disp->video_context->lpVtbl->VideoProcessorBlt(
		disp->video_context,
		disp->video_processor,
		output_view,
		0,
		1,
		&stream
	);
	check_hresult(hr, L"Failed to convert captured frame to NV12");
	release_com_obj(output_view);
	release_com_obj(input_view);

	d3d->context->lpVtbl->Flush(d3d->context);

	IMFMediaBuffer * mf_buffer;
	hr = MFCreateDXGISurfaceBuffer(
		&IID_ID3D11Texture2D,
		(IUnknown *)nv12_dxgi_surface,
		0,
		FALSE,
		&mf_buffer
	);
	check_hresult(hr, L"Failed to create MF DXGI surface buffer");
	acquire_com_obj(mf_buffer, L"mf_buffer");
	release_com_obj(nv12_dxgi_surface);

	IMFSample * sample;
	hr = MFCreateSample(&sample);
	check_hresult(hr, L"Failed to create MF sample");
	acquire_com_obj(sample, L"sample");

	hr = sample->lpVtbl->AddBuffer(sample, mf_buffer);
	check_hresult(hr, L"Failed to add buffer to sample");
	release_com_obj(mf_buffer);

	hr = sample->lpVtbl->SetSampleTime(sample, time);
	check_hresult(hr, L"Failed to set sample time");

	hr = sample->lpVtbl->SetSampleDuration(sample, duration);
	check_hresult(hr, L"Failed to set sample duration");

	release_frame(disp);

	return sample;
}

void release_events(MFT_OUTPUT_DATA_BUFFER * output_buf) {
	if (output_buf->pEvents) {
		Release(output_buf->pEvents);
		output_buf->pEvents = NULL;
	}
}

void select_output_type(struct mf_state * mf) {
	IMFTransform * enc = mf->d3d->enc->encoder;
	IMFAttributes * old_attrs = (IMFAttributes *)mf->out_type;
	DWORD i = 0;

	GUID old_major_type;
	GUID old_subtype;
	UINT32 old_interlace;

	HRESULT hr = GetGUID(old_attrs, &MF_MT_MAJOR_TYPE, &old_major_type);
	hr |= GetGUID(old_attrs, &MF_MT_SUBTYPE, &old_subtype);
	hr |= GetUINT32(old_attrs, &MF_MT_INTERLACE_MODE, &old_interlace);
	check_hresult(hr, L"Failed to query attributes of old output type");

	while (1) {
		IMFMediaType * type;
		hr = enc->lpVtbl->GetOutputAvailableType(
			enc,
			mf->out_stream_id,
			i,
			&type
		);

		if (hr == MF_E_NO_MORE_TYPES) {
			break;
		}
		check_hresult(hr, L"Failed to get encoder output types");
		acquire_com_obj(type, L"type");

		IMFAttributes * new_attrs = (IMFAttributes *)type;
		GUID new_major_type;
		GUID new_subtype;
		UINT32 new_interlace;

		hr = GetGUID(new_attrs, &MF_MT_MAJOR_TYPE, &new_major_type);
		hr |= GetGUID(new_attrs, &MF_MT_SUBTYPE, &new_subtype);
		hr |= GetUINT32(new_attrs, &MF_MT_INTERLACE_MODE, &new_interlace);
		check_hresult(hr, L"Failed to get encoder output type attributes");

		if (
			IsEqualGUID(&old_major_type, &new_major_type) &&
			IsEqualGUID(&old_subtype, &new_subtype) &&
			old_interlace == new_interlace
		) {
			release_com_obj(mf->out_type);
			mf->out_type = type;

			hr = enc->lpVtbl->SetOutputType(
				enc,
				mf->out_stream_id,
				mf->out_type,
				0
			);
			check_hresult(hr, L"Failed to set output media type");
			return;
		}

		i++;
		release_com_obj(type);
	}

	print_err_fmt(L"No available output types matching desired output type:\n");
	print_attrs(old_attrs);
	exit_process(1);
}

void handle_stream_change(
	struct mf_state * mf,
	struct mp4_file * mp4
) {
	IMFTransform * enc = mf->d3d->enc->encoder;
	struct in_out_stream_ids stream_ids = select_streams(enc);

	mf->in_stream_id = stream_ids.in_stream_id;
	mf->out_stream_id = stream_ids.out_stream_id;

	select_output_type(mf);
	create_mp4_sink(mf, mp4);
}

BOOL process_mft_events(
	struct mf_state * mf,
	struct mp4_file * mp4,
	MFT_OUTPUT_DATA_BUFFER * output_buf
) {
	IMFTransform * enc = mf->d3d->enc->encoder;
	IMFMediaEvent * event;

	while (1) {
		HRESULT hr = mf->event_gen->lpVtbl->GetEvent(mf->event_gen, MF_EVENT_FLAG_NO_WAIT, &event);

		if (hr == MF_E_NO_EVENTS_AVAILABLE) {
			break;
		}
		check_hresult(hr, L"Failed to get encoder events");
		acquire_com_obj(event, L"event");

		MediaEventType type;
		hr = event->lpVtbl->GetType(event, &type);
		check_hresult(hr, L"Failed to get event type");

		if (type == METransformHaveOutput) {
			DWORD output_status;

			output_buf->pSample = NULL;
			output_buf->dwStatus = 0;
			output_buf->pEvents = NULL;

			hr = enc->lpVtbl->ProcessOutput(enc, 0, 1, output_buf, &output_status);
			if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
				handle_stream_change(mf, mp4);
			} else {
				check_hresult(hr, L"Failed to get encoder output");
			}

			if (output_buf->pSample) {
				acquire_com_obj(output_buf->pSample, L"output_buf->pSample");

				hr = mp4->sink->lpVtbl->ProcessSample(mp4->sink, output_buf->pSample);
				check_hresult(hr, L"Failed to process sample");

				release_com_obj(output_buf->pSample);
			}

			release_events(output_buf);
		} else if (type == METransformDrainComplete) {
			break;
		}

		release_com_obj(event);
	}

	return TRUE;
}

void capture_screen(
	struct display * disp,
	struct mf_state * mf,
	struct mp4_file * mp4,
	long long duration_s,
	long long target_fps 
) {
	static const long long ticks_per_s = 10000000;

	MFT_OUTPUT_DATA_BUFFER output_buf = {
		.dwStreamID = mf->out_stream_id
		// TODO: Support encoders that don't allocate output samples
	};

	const long long frame_interval = ticks_per_s / target_fps;
	const long long video_len = duration_s * ticks_per_s;

	struct hw_encoder * enc = mf->d3d->enc;

	LARGE_INTEGER freq;
	LARGE_INTEGER now;

	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&now);

	const long long f = ticks_per_s / freq.QuadPart;
	const long long start_ticks = now.QuadPart * f;
	long long now_ticks = start_ticks;
	long long frame_ticks = start_ticks;
	long long next_frame_target = frame_ticks + frame_interval;
	long long duration;
	int i = 0;

	if (! mf->allocates_samples) {
		// TODO: Allocate samples for such encoders
		print_err_fmt(L"Encoder does not allocate samples");
		exit_process(1);
	}

	HRESULT hr = enc->encoder->lpVtbl->ProcessMessage(enc->encoder, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
	check_hresult(hr, L"Failed to begin streaming (1)");

	hr = enc->encoder->lpVtbl->ProcessMessage(enc->encoder, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
	check_hresult(hr, L"Failed to begin streaming (2)");

	while (now_ticks < (start_ticks + video_len)) {
		BOOL can_accept_frame = process_mft_events(mf, mp4, &output_buf);

		QueryPerformanceCounter(&now);

		now_ticks = now.QuadPart * f;

		if (now_ticks > next_frame_target && can_accept_frame) {
			const long long t = frame_ticks;
			duration = now_ticks - frame_ticks;
			frame_ticks = now_ticks;
			next_frame_target = frame_ticks + frame_interval;

			IMFSample * sample = capture_video_frame(disp, mf, t - start_ticks, duration);
			hr = enc->encoder->lpVtbl->ProcessInput(enc->encoder, mf->in_stream_id, sample, 0);
			check_hresult(hr, L"Failed to add sample");
			release_com_obj(sample);

			i++;
		}

		Sleep(0);
	}

	process_mft_events(mf, mp4, &output_buf);

	hr = enc->encoder->lpVtbl->ProcessMessage(enc->encoder, MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
	check_hresult(hr, L"Failed to end streaming");

	hr = mp4->sink->lpVtbl->Flush(mp4->sink);
	check_hresult(hr, L"Failed to flush mp4 sink");

	hr = mp4->sink->lpVtbl->PlaceMarker(
		mp4->sink,
		MFSTREAMSINK_MARKER_ENDOFSEGMENT,
		NULL,
		&mp4->end_of_segment_val
	);
	check_hresult(hr, L"Failed to place end of segment marker");

	enum semaphore_status marker_status = wait_for_end_of_segment(
		(struct mp4_event_callback *)mp4->event_callback,
		30 * 1000
	);

	if (marker_status == Timeout) {
		print_err_fmt(L"Timed out waiting for end of segment marker");
		exit_process(1);
	} else if (marker_status == Interrupted) {
		print_err_fmt(L"mp4 event thread was interrupted");
		exit_process(1);
	}

	IMFFinalizableMediaSink * finalizable;
	hr = QueryInterface(mp4->media_sink, &IID_IMFFinalizableMediaSink, (void **)&finalizable);
	check_hresult(hr, L"Failed to get finalizable media sink");
	acquire_com_obj(finalizable, L"finalizable");

	IMFAsyncResult * result;
	struct mp4_finalize_callback * finalizer = mp4_finalize_callback_new(finalizable);
	acquire_com_obj(finalizer, L"finalizer");
	release_com_obj(finalizable);

	hr = MFCreateAsyncResult(
		NULL,
		(IMFAsyncCallback *)finalizer,
		NULL,
		&result
	);
	check_hresult(hr, L"Failed to create async result");
	acquire_com_obj(result, L"result");

	print_fmt(L"Finalizing mp4\n");
	start_finalization(finalizer);

	enum semaphore_status finalizer_status = wait_for_finalization(finalizer, 30 * 1000);

	if (finalizer_status == Timeout) {
		print_err_fmt(L"Finalizer timed out after 30 seconds");
		exit_process(1);
	} else if (finalizer_status == Interrupted) {
		print_err_fmt(L"Finalizer was interrupted");
		exit_process(1);
	}

	print_fmt(L"Finalization done\n");

	release_com_obj(finalizer);
	release_com_obj(result);

	hr = mp4->clock->lpVtbl->Stop(mp4->clock);
	check_hresult(hr, L"Failed to stop presentation clock");

	hr = mp4->media_sink->lpVtbl->Shutdown(mp4->media_sink);
	check_hresult(hr, L"Failed to shut down media sink");
}

void free_hw_encoder(struct hw_encoder * enc) {
	static const struct hw_encoder zero = { 0 };

	if (enc->name) {
		CoTaskMemFree(enc->name);
	}

	if (enc->activate) {
		release_com_obj(enc->activate);
	}

	if (enc->encoder) {
		release_com_obj(enc->encoder);
	}

	(*enc) = zero;
}

void free_d3d(struct d3d * d3d) {
	static const struct d3d zero = { 0 };

	if (d3d->enc) {
		free_hw_encoder(d3d->enc);
	}

	if (d3d->dxgi_device) {
		release_com_obj(d3d->dxgi_device);
	}

	if (d3d->dxgi_adapter) {
		release_com_obj(d3d->dxgi_adapter);
	}

	if (d3d->device) {
		release_com_obj(d3d->device);
	}

	if (d3d->context) {
		release_com_obj(d3d->context);
	}

	(*d3d) = zero;
}

void free_display(struct display * disp) {
	static const struct display zero = { 0 };

	if (disp->d3d) {
		free_d3d(disp->d3d);
	}

	if (disp->output) {
		release_com_obj(disp->output);
	}

	if (disp->output1) {
		release_com_obj(disp->output1);
	}

	if (disp->dup) {
		release_com_obj(disp->dup);
	}

	if (disp->video_device) {
		release_com_obj(disp->video_device);
	}

	if (disp->video_context) {
		release_com_obj(disp->video_context);
	}

	if (disp->video_processor_enum) {
		release_com_obj(disp->video_processor_enum);
	}

	if (disp->video_processor) {
		release_com_obj(disp->video_processor);
	}

	if (disp->frame) {
		release_frame(disp);
	}

	(*disp) = zero;
}

void free_mf_state(struct mf_state * mf) {
	static const struct mf_state zero = { 0 };

	if (mf->d3d) {
		free_d3d(mf->d3d);
	}

	if (mf->h_d3d_device != INVALID_HANDLE_VALUE) {
		if (mf->device_manager) {
			mf->device_manager->lpVtbl->CloseDeviceHandle(mf->device_manager, mf->h_d3d_device);
		}
	}

	if (mf->device_manager) {
		release_com_obj(mf->device_manager);
	}

	if (mf->out_type) {
		release_com_obj(mf->out_type);
	}

	if (mf->in_type) {
		release_com_obj(mf->in_type);
	}

	if (mf->event_gen) {
		release_com_obj(mf->event_gen);
	}

	(*mf) = zero;
	mf->h_d3d_device = INVALID_HANDLE_VALUE;
}

void free_mp4_file(struct mp4_file * mp4) {
	static const struct mp4_file zero = { 0 };

	if (mp4->file) {
		release_com_obj(mp4->file);
	}

	if (mp4->media_sink) {
		release_com_obj(mp4->media_sink);
	}

	if (mp4->sink) {
		release_com_obj(mp4->sink);
	}

	if (mp4->clock) {
		mp4->clock->lpVtbl->Stop(mp4->clock);
		release_com_obj(mp4->clock);
	}

	if (mp4->event_callback) {
		release_com_obj(mp4->event_callback);
	}

	PropVariantClear(&mp4->end_of_segment_val);

	(*mp4) = zero;
}

void print_attrs(IMFAttributes * attrs) {
	static wchar_t buf[4096];
	attrs->lpVtbl->LockStore(attrs);

	UINT32 count;
	HRESULT hr = attrs->lpVtbl->GetCount(attrs, &count);
	check_hresult(hr, L"Failed to get IMFAttributes count");

	for (UINT32 i = 0; i < count; i++) {
		GUID key;
		PROPVARIANT val;
		hr = attrs->lpVtbl->GetItemByIndex(attrs, i, &key, &val);
		check_hresult(hr, L"Failed to get IMFAttributes attr");

		const wchar_t * key_str = get_guid_name(&key);

		if (! key_str) {
			hr = StringFromCLSID(&key, &key_str);
			check_hresult(hr, L"Failed to stringify GUID");
		}

		print_fmt(L"  %1!s! = ", key_str);
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

		print_fmt(L"%1!s! (vt = %2!d!)\n", buf, val.vt);

		PropVariantClear(&val);
	}
}