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
#include "input.h"
#include "logging.h"
#include <propvarutil.h>
#include <codecapi.h>
#include <Mferror.h>

// DEADBEEF-1234-4567-DEAD-BEEFAAAAAAAA
// UINT32 on IMFSample: stores the sample's index in the NV12 pool
DEFINE_GUID(PRIVATE_SAMPLE_BUF_IDX,
	0xDEADBEEF, 0x1234, 0x4567, 0xDE, 0xAD, 0xBE, 0xEF, 0xAA, 0xAA, 0xAA, 0xAA);

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
	log_verbose(L"Initialized Media Foundation\n");
}

void list_encoders() {
	wchar_t clsid_str[64];

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

	log_info(L"===== Available H.264 hardware encoders by ID =====\n");

	for (unsigned int i = 0; i < count; i++) {
		IMFActivate * activate = activate_arr[i];
		GUID clsid;
		wchar_t * name;
		UINT32 name_len;

		hr |= GetGUID(activate, &MFT_TRANSFORM_CLSID_Attribute, &clsid);
		hr |= activate->lpVtbl->GetAllocatedString(activate, &MFT_FRIENDLY_NAME_Attribute, &name, &name_len);

		if (! SUCCEEDED(hr)) {
			hr = S_OK;
			continue;
		}
		acquire_com_str(name, L"name");

		int clsid_str_size = StringFromGUID2(&clsid, clsid_str, ARR_SIZE(clsid_str));

		if (! clsid_str_size) {
			log_err(L"Class ID for encoder \"%1!s!\" was too long", name);
			exit_process(1);
		}

		// StringFromGUID2 puts curly braces around the GUID and we don't want those;
		// GUID is 36 chars long
		clsid_str[37] = L'\0';

		log_info(L"  [%1!s!] %2!s!\n", clsid_str + 1, name);
		print_attrs(Verbose, 4, (IMFAttributes*)activate);

		release_com_obj(name);
	}

	release_com_obj(activate_arr);
}

struct hw_encoder select_encoder(struct args * args) {
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

	UINT best_encoder_idx = -1;
	struct hw_encoder best = {
		.args = args
	};

	if (args->encoder_clsid_str[0]) {
		hr = CLSIDFromString(args->encoder_clsid_str, &args->encoder_clsid);

		if (hr == CO_E_CLASSSTRING) {
			log_err(L"Encoder id \"%1!s!\" is not a GUID\n", args->encoder_clsid_str);
			exit_process(1);
		}

		check_hresult(hr, L"Failed to convert encoder argument to class ID");
	}

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

		acquire_com_str(name, L"name");

		enum gpu_vendor vendor = Unknown;

		if (find_wstr(name, L"Intel") != -1) {
			vendor = Intel;
		} else if (find_wstr(name, L"AMD") != -1) {
			vendor = AMD;
		} else if (find_wstr(name, L"NVIDIA") != -1) {
			vendor = Nvidia;
		}

		BOOL clsid_is_equal = IsEqualGUID(&clsid, &args->encoder_clsid);

		if (
			clsid_is_equal ||
			vendor > best.vendor ||
			(vendor == best.vendor && merit > best.merit)
		) {
			best_encoder_idx = i;
			best.activate = activate;
			best.vendor = vendor;
			best.merit = merit;

			if (best.name) {
				release_com_obj(best.name);
			}

			best.name = name;

			if (clsid_is_equal) {
				break;
			}
		} else {
			release_com_obj(name);
		}
	}

	if (best.activate) {
		activate_arr[best_encoder_idx] = NULL;
		acquire_com_obj(best.activate, L"best.activate");
	}

	release_com_obj(activate_arr);

	if (best_encoder_idx == -1) {
		log_err(L"Failed to find a suitable encoder\n");

		if (args->encoder_clsid_str[0]) {
			log_err(L"(No encoders with ID \"%1!s!\"\n", args->encoder_clsid_str);
		}

		exit_process(1);
	}

	best.is_initialized = 1;

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
			if (find_wstr(desc.Description, L"Intel") != -1) {
				vendor = Intel;
			} else if (find_wstr(desc.Description, L"AMD") != -1) {
				vendor = AMD;
			} else if (find_wstr(desc.Description, L"NVIDIA") != -1) {
				vendor = Nvidia;
			}
		}

		if (vendor == Unknown) {
			log_err(L"Unknown DXGI adapter vendor (0x%1!x!): %2!s!\n", desc.VendorId, desc.Description);
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

	d3d.is_initialized = 1;

	return d3d;
}

// Creates the output segments of the BGRA8 -> NV12 pipeline, as well as the backup
// NV12 texture. This only needs to be created once.
static void create_nv12_conv_pool(struct display * disp) {
	struct d3d * d3d = disp->d3d;
	struct args * args = d3d->enc->args;

	D3D11_TEXTURE2D_DESC nv12_desc = {
		.Width = disp->width,
		.Height = disp->height,
		.MipLevels = 1,
		.ArraySize = 1,
		.Usage = D3D11_USAGE_DEFAULT,
		.SampleDesc = {
			.Count = 1
		},
		.Format = DXGI_FORMAT_NV12,
		.CPUAccessFlags = 0,
		.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
		.MiscFlags = 0
	};

	disp->nv12_conv_pool = alloc_or_die(args->pool_size * sizeof(struct nv12_conv));
	disp->nv12_pool_size = args->pool_size;
	memset(disp->nv12_conv_pool, 0, disp->nv12_pool_size * sizeof(struct nv12_conv));

	for (unsigned int i = 0; i < disp->nv12_pool_size; i++) {
		struct nv12_conv * conv = disp->nv12_conv_pool + i;

		conv->is_free = TRUE;

		HRESULT hr = d3d->device->lpVtbl->CreateTexture2D(d3d->device, &nv12_desc, NULL, &conv->nv12_tex);
		check_hresult(hr, L"Failed to create NV12 texture");
		acquire_com_obj(conv->nv12_tex, L"conv->nv12_tex");

		hr = QueryInterface(conv->nv12_tex, &IID_IDXGISurface, (void **)&conv->nv12_dxgi_surface);
		check_hresult(hr, L"Failed to get IDXGISurface from NV12 texture");
		acquire_com_obj(conv->nv12_dxgi_surface, L"conv->nv12_dxgi_surface");

		D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_view_desc = {
			.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D
		};

		hr = disp->video_device->lpVtbl->CreateVideoProcessorOutputView(
			disp->video_device,
			(ID3D11Resource *)conv->nv12_tex,
			disp->video_processor_enum,
			&output_view_desc,
			&conv->output_view
		);
		check_hresult(hr, L"Failed to create video processor output view");
		acquire_com_obj(conv->output_view, L"conv->output_view");

		hr = MFCreateDXGISurfaceBuffer(
			&IID_ID3D11Texture2D,
			(IUnknown *)conv->nv12_dxgi_surface,
			0,
			FALSE,
			&conv->mf_buffer
		);
		check_hresult(hr, L"Failed to create MF DXGI surface buffer");
		acquire_com_obj(conv->mf_buffer, L"conv->mf_buffer");
	}

	HRESULT hr = d3d->device->lpVtbl->CreateTexture2D(d3d->device, &nv12_desc, NULL, &disp->prev_nv12_frame);
	check_hresult(hr, L"Failed to create backup NV12 texture");
	acquire_com_obj(disp->prev_nv12_frame, L"disp->prev_nv12_frame");
}

struct display select_display(struct d3d * d3d) {
	struct args * args = d3d->enc->args;

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

		if (i == args->display) {
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

	create_nv12_conv_pool(&disp);

	disp.is_initialized = 1;

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

	mf.is_initialized = 1;

	return mf;
}

void prepare_for_streaming(struct display * disp, struct mf_state * mf) {
	struct hw_encoder * enc = mf->d3d->enc;
	struct args * args = enc->args;

	HRESULT hr = MFCreateMediaType(&mf->out_type);
	check_hresult(hr, L"Failed to create video disp type");
	acquire_com_obj(mf->out_type, L"mf->out_type");

	hr = SetGUID(mf->out_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
	hr |= SetGUID(mf->out_type, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
	hr |= SetUINT32(mf->out_type, &MF_MT_MPEG2_PROFILE, args->profile);
	hr |= SetUINT32(mf->out_type, &MF_MT_AVG_BITRATE, args->bitrate);
	hr |= SetUINT32(mf->out_type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	hr |= MFSetAttributeSize((IMFAttributes *)mf->out_type, &MF_MT_FRAME_SIZE, disp->width, disp->height);
	hr |= MFSetAttributeRatio((IMFAttributes *)mf->out_type, &MF_MT_FRAME_RATE, args->fps, 1);
	hr |= MFSetAttributeRatio((IMFAttributes *)mf->out_type, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	check_hresult(hr, L"Failed to set attributes for output type");


	hr = enc->encoder->lpVtbl->SetOutputType(enc->encoder, mf->out_stream_id, mf->out_type, 0);
	check_hresult(hr, L"Failed to set output type");

	hr = MFCreateMediaType(&mf->in_type);
	check_hresult(hr, L"Failed to create input type");
	acquire_com_obj(mf->in_type, L"mf->in_type");

	hr = SetGUID(mf->in_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
	hr |= SetGUID(mf->in_type, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
	hr |= SetUINT32(mf->in_type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	hr |= MFSetAttributeSize((IMFAttributes *)mf->in_type, &MF_MT_FRAME_SIZE, disp->width, disp->height);
	hr |= MFSetAttributeRatio((IMFAttributes *)mf->in_type, &MF_MT_FRAME_RATE, args->fps, 1);
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
		log_err(L"Only rateless media sinks are supported");
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

	log_debug(L"Output media type: \n");
	print_attrs(Debug, 2, (IMFAttributes *)mf->out_type);

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

void set_mp4_output_type(struct mf_state* mf, struct mp4_file* mp4) {
	IMFMediaTypeHandler * media_type_handler;
	HRESULT hr = mp4->sink->lpVtbl->GetMediaTypeHandler(mp4->sink, &media_type_handler);
	check_hresult(hr, L"Failed to get mp4 sink media type handler");
	acquire_com_obj(media_type_handler, L"media_type_handler");

	hr = media_type_handler->lpVtbl->SetCurrentMediaType(media_type_handler, mf->out_type);
	check_hresult(hr, L"Failed to set mp4 media type");

	release_com_obj(media_type_handler);
}

struct mp4_file create_mp4_file(struct mf_state * mf, const wchar_t * name) {
	struct mp4_file mp4 = {
		.name = name
	};

	PropVariantInit(&mp4.end_of_segment_val);
	mp4.end_of_segment_val.vt = VT_UI4;
	mp4.end_of_segment_val.ulVal = MFSTREAMSINK_MARKER_ENDOFSEGMENT;

	create_mp4_sink(mf, &mp4);

	return mp4;
}

static void release_frame(struct display * disp, ID3D11Texture2D * frame) {
	release_com_obj(frame);
	disp->dup->lpVtbl->ReleaseFrame(disp->dup);
}

// (Re)creates the input segment of the BGRA8 -> NV12 pipeline. We only need
// to recreate this when the frame pointer returned by the D3D duplication API
// has changed.
static void create_nv12_conv_input(struct display * disp, ID3D11Texture2D * frame) {
	struct d3d * d3d = disp->d3d;

	if (disp->input_view) {
		release_com_obj(disp->input_view);
		disp->input_view = NULL;
	}

	disp->prev_dup_frame = frame;

	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_view_desc = {
		.FourCC = 0,
		.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D
	};

	HRESULT hr = disp->video_device->lpVtbl->CreateVideoProcessorInputView(
		disp->video_device,
		(ID3D11Resource *)frame,
		disp->video_processor_enum,
		&input_view_desc,
		&disp->input_view
	);
	check_hresult(hr, L"Failed to create video processor input view");
	acquire_com_obj(disp->input_view, L"disp->input_view");

	disp->stream = (D3D11_VIDEO_PROCESSOR_STREAM){
		.Enable = TRUE,
		.pInputSurface = disp->input_view
	};
}

int find_available_nv12_conv(struct display * disp) {
	for (unsigned int i = 0; i < disp->nv12_pool_size; i++) {
		if (disp->nv12_conv_pool[i].is_free) {
			return i;
		}
	}

	return -1;
}

static struct nv12_conv * capture_video_frame(
	struct display * disp,
	struct mf_state * mf,
	LONGLONG time,
	LONGLONG duration
) {
	const struct d3d * d3d = mf->d3d;

	int nv12_frame_idx = find_available_nv12_conv(disp);

	if (nv12_frame_idx == -1) {
		log_warn(L"No more NV12 output frames available\n");

		return NULL;
	}

	struct nv12_conv * conv = disp->nv12_conv_pool + nv12_frame_idx;
	conv->is_free = FALSE;

	DXGI_OUTDUPL_FRAME_INFO frame_info;
	IDXGIResource * desktop_resource = NULL;
	ID3D11Texture2D * frame;

	HRESULT hr = disp->dup->lpVtbl->AcquireNextFrame(disp->dup, 1, &frame_info, &desktop_resource);
	if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
		d3d->context->lpVtbl->CopyResource(
			d3d->context,
			(ID3D11Resource *)conv->nv12_tex,
			(ID3D11Resource *)disp->prev_nv12_frame
		);
	} else {
		check_hresult(hr, L"Failed to acquire next frame");
		acquire_com_obj(desktop_resource, L"desktop_resource");

		hr = QueryInterface(desktop_resource, &IID_ID3D11Texture2D, (void **)&frame);
		check_hresult(hr, L"Failed to get frame as texture");
		acquire_com_obj(frame, L"frame");

		if (frame != disp->prev_dup_frame) {
			log_verbose(L"Recreating BGRA8 -> NV12 conversion input\n");
			create_nv12_conv_input(disp, frame);
		}

		hr = disp->video_context->lpVtbl->VideoProcessorBlt(
			disp->video_context,
			disp->video_processor,
			conv->output_view,
			0,
			1,
			&disp->stream
		);
		check_hresult(hr, L"Failed to convert captured frame to NV12");

		d3d->context->lpVtbl->CopyResource(
			d3d->context,
			(ID3D11Resource *)disp->prev_nv12_frame,
			(ID3D11Resource *)conv->nv12_tex
		);
	}

	d3d->context->lpVtbl->Flush(d3d->context);

	if (conv->sample) {
		release_com_obj(conv->sample);
	}

	hr = MFCreateSample(&conv->sample);
	check_hresult(hr, L"Failed to create MF sample");
	acquire_com_obj(conv->sample, L"nv12_frame->sample");

	hr = conv->sample->lpVtbl->AddBuffer(conv->sample, conv->mf_buffer);
	check_hresult(hr, L"Failed to add buffer to sample");

	hr = conv->sample->lpVtbl->SetSampleTime(conv->sample, time);
	check_hresult(hr, L"Failed to set sample time");

	hr = conv->sample->lpVtbl->SetSampleDuration(conv->sample, duration);
	check_hresult(hr, L"Failed to set sample duration");

	hr = SetUINT32(conv->sample, &PRIVATE_SAMPLE_BUF_IDX, nv12_frame_idx);
	check_hresult(hr, L"Failed to tag sample with buffer index");

	if (desktop_resource) {
		release_frame(disp, frame);
		release_com_obj(desktop_resource);
	}

	return conv;
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

	log_err(L"No available output types matching desired output type:\n");
	log_debug(L"Old output type:\n");
	print_attrs(Debug, 2, old_attrs);
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
	set_mp4_output_type(mf, mp4);
}

BOOL process_mft_events(
	struct mf_state * mf,
	struct mp4_file * mp4,
	struct display * disp,
	MFT_OUTPUT_DATA_BUFFER * output_buf
) {
	const unsigned int max_rejected_samples = disp->nv12_pool_size;

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

				unsigned int rejected_samples = 0;

				while (rejected_samples < max_rejected_samples) {
					hr = mp4->sink->lpVtbl->ProcessSample(mp4->sink, output_buf->pSample);
					if (hr == MF_E_NOTACCEPTING) {
						rejected_samples++;
						Sleep(1);
					} else {
						check_hresult(hr, L"Failed to process sample");
						break;
					}
				}

				if (rejected_samples >= max_rejected_samples) {
					log_err(L"Too many samples rejected by mp4 sink (%1!d!)\n", rejected_samples);
					exit_process(1);
				}

				DWORD nv12_conv_idx;
				hr = GetUINT32(output_buf->pSample, &PRIVATE_SAMPLE_BUF_IDX, &nv12_conv_idx);
				check_hresult(hr, L"Failed to get sample pool slot index tag");

				if (nv12_conv_idx >= disp->nv12_pool_size) {
					log_err(L"Sample pool slot index was unexpectedly out of bounds\n");
					exit_process(1);
				}

				disp->nv12_conv_pool[nv12_conv_idx].is_free = TRUE;

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
	const volatile BOOL * termination_signal,
	volatile BOOL * is_ready_to_record
) {
	static const long long ticks_per_s = 10000000;

	const unsigned int max_rejected_frames = disp->nv12_pool_size;

	MFT_OUTPUT_DATA_BUFFER output_buf = {
		.dwStreamID = mf->out_stream_id
		// TODO: Support encoders that don't allocate output samples
	};

	struct args * args = mf->d3d->enc->args;

	const long long target_fps = args->fps;
	const long long frame_interval = ticks_per_s / target_fps;

	struct hw_encoder * enc = mf->d3d->enc;

	while (! mp4->is_recording) {
		*is_ready_to_record = TRUE;
		process_messages();

		Sleep(1);

		if (*termination_signal) {
			exit_process(0);
		}
	}

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
		log_err(L"Encoder does not allocate samples");
		exit_process(1);
	}

	HRESULT hr = enc->encoder->lpVtbl->ProcessMessage(enc->encoder, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
	check_hresult(hr, L"Failed to begin streaming (1)");

	hr = enc->encoder->lpVtbl->ProcessMessage(enc->encoder, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
	check_hresult(hr, L"Failed to begin streaming (2)");

	while (mp4->is_recording) {
		process_messages();

		if (! mp4->is_recording) {
			break;
		}

		if (*termination_signal) {
			exit_process(0);
		}

		BOOL can_accept_frame = process_mft_events(mf, mp4, disp, &output_buf);

		QueryPerformanceCounter(&now);

		now_ticks = now.QuadPart * f;

		if (now_ticks > next_frame_target && can_accept_frame) {
			const long long t = frame_ticks;
			duration = now_ticks - frame_ticks;
			frame_ticks = now_ticks;
			next_frame_target = frame_ticks + frame_interval;

			unsigned int rejected_frames = 0;
			unsigned int pool_full_frames = 0;

			while (rejected_frames < max_rejected_frames && pool_full_frames < max_rejected_frames) {
				struct nv12_conv * conv = capture_video_frame(disp, mf, t - start_ticks, duration);
				if (! conv) {
					pool_full_frames++;
					process_mft_events(mf, mp4, disp, &output_buf);
					Sleep(1);
					break;
				}

				hr = enc->encoder->lpVtbl->ProcessInput(enc->encoder, mf->in_stream_id, conv->sample, 0);

				if (hr == MF_E_NOTACCEPTING) {
					rejected_frames++;
					process_mft_events(mf, mp4, disp, &output_buf);
				} else {
					check_hresult(hr, L"Failed to add sample");
					release_com_obj(conv->sample);
					conv->sample = NULL;
					break;
				}
			}

			if (rejected_frames >= max_rejected_frames) {
				log_err(L"Too many frames rejected by H.264 encoder (%1!d!)\n", rejected_frames);
				exit_process(1);
			}

			if (pool_full_frames >= max_rejected_frames) {
				log_err(L"NV12 converter pool was full for too long. Try increasing the pool size with --pool-size (current size is %1!d!)\n", disp->nv12_pool_size);
				exit_process(1);
			}

			i++;
		}

		Sleep(1);
	}

	process_mft_events(mf, mp4, disp, &output_buf);

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
		log_err(L"Timed out waiting for end of segment marker");
		exit_process(1);
	} else if (marker_status == Interrupted) {
		log_err(L"mp4 event thread was interrupted");
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

	log_info(L"Finalizing mp4\n");
	start_finalization(finalizer);

	enum semaphore_status finalizer_status = wait_for_finalization(finalizer, 30 * 1000);

	if (finalizer_status == Timeout) {
		log_err(L"Finalizer timed out after 30 seconds");
		exit_process(1);
	} else if (finalizer_status == Interrupted) {
		log_err(L"Finalizer was interrupted");
		exit_process(1);
	}

	log_info(L"Finalization done\n");

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
		release_com_obj(enc->name);
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

	if (disp->prev_nv12_frame) {
		release_com_obj(disp->prev_nv12_frame);
	}

	if (disp->input_view) {
		release_com_obj(disp->input_view);
	}

	for (unsigned int i = 0; i < disp->nv12_pool_size; i++) {
		struct nv12_conv * conv = disp->nv12_conv_pool + i;

		if (conv->nv12_tex) {
			release_com_obj(conv->nv12_tex);
		}

		if (conv->nv12_dxgi_surface) {
			release_com_obj(conv->nv12_dxgi_surface);
		}

		if (conv->output_view) {
			release_com_obj(conv->output_view);
		}

		if (conv->mf_buffer) {
			release_com_obj(conv->mf_buffer);
		}

		if (conv->sample) {
			release_com_obj(conv->sample);
		}
	}

	dealloc(disp->nv12_conv_pool);

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

static void indent(enum log_level log_lvl, int indent_lvl) {
	for (int i = 0; i < indent_lvl; i++) {
		print_lvl_fmt(log_lvl, L" ");
	}
}

void print_attrs(enum log_level log_lvl, int indent_lvl, IMFAttributes * attrs) {
	static wchar_t buf[4096];

	if (log_lvl > log_level) {
		return;
	}

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
		wchar_t * key_guid_str = NULL;

		if (! key_str) {
			hr = StringFromCLSID(&key, &key_guid_str);
			check_hresult(hr, L"Failed to stringify GUID");
			acquire_com_str(key_guid_str, L"key_guid_str");
			key_str = key_guid_str;
		}

		indent(log_lvl, indent_lvl);
		print_lvl_fmt(log_lvl, L"%1!s! = ", key_str);
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

		if (IsEqualGUID(&key, &MFT_INPUT_TYPES_Attributes) || IsEqualGUID(&key, &MFT_OUTPUT_TYPES_Attributes)) {
			if (val.vt != (VT_VECTOR | VT_UI1)) {
				log_err(L"Expected %1!s! to be a vector of unsigned chars (vt = %2!d!)\n", key_str, val.vt);
				continue;
			}

			int num_types = val.cai.cElems / sizeof(MFT_REGISTER_TYPE_INFO);
			MFT_REGISTER_TYPE_INFO * type_infos = (MFT_REGISTER_TYPE_INFO *)val.cai.pElems;

			print_lvl_fmt(log_lvl, L"[\n");

			for (int j = 0; j < num_types; j++) {
				indent(log_lvl, indent_lvl + 2);
				print_lvl_fmt(log_lvl, L"{\n");
				const wchar_t * major_type_str = get_guid_name(&type_infos[j].guidMajorType);
				wchar_t * major_type_guid_str = NULL;

				if (! major_type_str) {
					hr = StringFromCLSID(&type_infos[j].guidMajorType, &major_type_guid_str);
					check_hresult(hr, L"Failed to stringify major type GUID");
					acquire_com_str(major_type_guid_str, L"major_type_guid_str");
					major_type_str = major_type_guid_str;
				}

				const wchar_t * subtype_str = get_guid_name(&type_infos[j].guidSubtype);
				wchar_t * subtype_guid_str = NULL;

				if (! subtype_str) {
					hr = StringFromCLSID(&type_infos[j].guidSubtype, &subtype_guid_str);
					check_hresult(hr, L"Failed to stringify subtype GUID");
					acquire_com_str(subtype_guid_str, L"subtype_guid_str");
					subtype_str = subtype_guid_str;
				}

				indent(log_lvl, indent_lvl + 4);
				print_lvl_fmt(log_lvl, L"guidMajorType = %1!s!\n", major_type_str);

				indent(log_lvl, indent_lvl + 4);
				print_lvl_fmt(log_lvl, L"guidSubtype = %1!s!\n", subtype_str);

				if (major_type_guid_str) {
					release_com_obj(major_type_guid_str);
				}

				if (subtype_guid_str) {
					release_com_obj(subtype_guid_str);
				}

				indent(log_lvl, indent_lvl + 2);

				if (j == num_types - 1) {
					print_lvl_fmt(log_lvl, L"}\n");
				} else {
					print_lvl_fmt(log_lvl, L"},\n");
				}
			}

			indent(log_lvl, indent_lvl);
			print_lvl_fmt(log_lvl, L"]\n");
		} else {
			print_lvl_fmt(log_lvl, L"%1!s! (vt = %2!d!)\n", buf, val.vt);
		}

		if (key_guid_str) {
			release_com_obj(key_guid_str);
		}

		PropVariantClear(&val);
	}
}