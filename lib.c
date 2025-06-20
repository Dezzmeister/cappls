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
#include "lib.h"
#include "venc.h"
#include <mfapi.h>
#include <mftransform.h>
#include <ks.h>

struct guid_name_map {
    const GUID * guid;
    const wchar_t * name;
};

const GUID KSMFT_CATEGORY_VIDEO_ENCODER = {
    0xf79eac7d,
    0xe545,
    0x4387,
    { 0xbd, 0xee, 0xd6, 0x47, 0xd7, 0xbd, 0xe4, 0x2a }
};

static const struct guid_name_map guid_names[] = {
    { &MF_MT_MAJOR_TYPE,                       L"MF_MT_MAJOR_TYPE" },
    { &MF_MT_SUBTYPE,                          L"MF_MT_SUBTYPE" },
    { &MF_MT_FRAME_SIZE,                       L"MF_MT_FRAME_SIZE" },
    { &MF_MT_FRAME_RATE,                       L"MF_MT_FRAME_RATE" },
    { &MF_MT_PIXEL_ASPECT_RATIO,               L"MF_MT_PIXEL_ASPECT_RATIO" },
    { &MFT_TRANSFORM_CLSID_Attribute,          L"MFT_TRANSFORM_CLSID_Attribute" },
    { &MF_TRANSFORM_CATEGORY_Attribute,        L"MF_TRANSFORM_CATEGORY_Attribute" },
    { &MFT_INPUT_TYPES_Attributes,             L"MFT_INPUT_TYPES_Attributes" },
    { &MFT_OUTPUT_TYPES_Attributes,            L"MFT_OUTPUT_TYPES_Attributes" },
    { &MFT_ENUM_HARDWARE_URL_Attribute,        L"MFT_ENUM_HARDWARE_URL_Attribute" },
    { &MFT_ENUM_HARDWARE_VENDOR_ID_Attribute,  L"MFT_ENUM_HARDWARE_VENDOR_ID_Attribute" },
    { &MFT_SUPPORT_DYNAMIC_FORMAT_CHANGE,      L"MFT_SUPPORT_DYNAMIC_FORMAT_CHANGE" },
    { &MFT_ENUM_TRANSCODE_ONLY_ATTRIBUTE,      L"MFT_ENUM_TRANSCODE_ONLY_ATTRIBUTE" },
    { &MFT_ENUM_HARDWARE_URL_Attribute,        L"MFT_ENUM_HARDWARE_URL_Attribute" },
    { &MFT_FRIENDLY_NAME_Attribute,            L"MFT_FRIENDLY_NAME_Attribute" },
    { &MFT_PROCESS_LOCAL_Attribute,            L"MFT_PROCESS_LOCAL_Attribute" },
    { &MFT_PREFERRED_OUTPUTTYPE_Attribute,     L"MFT_PREFERRED_OUTPUTTYPE_Attribute" },
    { &MFT_CONNECTED_STREAM_ATTRIBUTE,         L"MFT_CONNECTED_STREAM_ATTRIBUTE" },
    { &MFT_SUPPORT_3DVIDEO,                    L"MFT_SUPPORT_3DVIDEO" },
    { &MFT_DECODER_EXPOSE_OUTPUT_TYPES_IN_NATIVE_ORDER, L"MFT_DECODER_EXPOSE_OUTPUT_TYPES_IN_NATIVE_ORDER" },
    { &MF_SA_D3D_AWARE,                        L"MF_SA_D3D_AWARE" },
    { &MF_SA_D3D11_BINDFLAGS,                  L"MF_SA_D3D11_BINDFLAGS" },
    { &MF_SA_D3D11_USAGE,                      L"MF_SA_D3D11_USAGE" },
    { &MF_SA_D3D11_AWARE,                      L"MF_SA_D3D11_AWARE" },
    { &MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT,      L"MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT" },
    { &MF_TRANSFORM_ASYNC,                     L"MF_TRANSFORM_ASYNC" },
    { &MF_TRANSFORM_ASYNC_UNLOCK,              L"MF_TRANSFORM_ASYNC_UNLOCK" },
    { &MF_TRANSFORM_FLAGS_Attribute,           L"MF_TRANSFORM_FLAGS_Attribute" },
    { &MF_TRANSFORM_CATEGORY_Attribute,        L"MF_TRANSFORM_CATEGORY_Attribute" },
    { &MFT_CODEC_MERIT_Attribute,              L"MFT_CODEC_MERIT_Attribute" },
    { &MFT_GFX_DRIVER_VERSION_ID_Attribute,    L"MFT_GFX_DRIVER_VERSION_ID_Attribute" },
    { &KSMFT_CATEGORY_VIDEO_ENCODER,           L"KSMFT_CATEGORY_VIDEO_ENCODER" },
    { &MF_MT_AVG_BITRATE,                      L"MF_MT_AVG_BITRATE" },
    { &MF_MT_MPEG2_PROFILE,                    L"MF_MT_MPEG2_PROFILE" },
    { &MF_MT_INTERLACE_MODE,                   L"MF_MT_INTERLACE_MODE" },
    { &CLSID_NVIDIA_H264_ENCODER,              L"CLSID_NVIDIA_H264_ENCODER" },
    { &CLSID_INTEL_QUICKSYNC_H264_ENCODER,     L"CLSID_INTEL_QUICKSYNC_H264_ENCODER" },
    { &PRIVATE_INTEL_PCI_DEVICE_INSTANCE,      L"PRIVATE_INTEL_PCI_DEVICE_INSTANCE" },
    { &MFMediaType_Video,                      L"MFMediaType_Video" },
    { &MFVideoFormat_NV12,                     L"MFVideoFormat_NV12" },
    { &MFVideoFormat_H264,                     L"MFVideoFormat_H264" },
    { &MF_MT_D3D_DEVICE,                       L"MF_MT_D3D_DEVICE" },
    { &MF_MT_MPEG_SEQUENCE_HEADER,             L"MF_MT_MPEG_SEQUENCE_HEADER" }
};

HANDLE std_out;
HANDLE std_err;
HANDLE heap;

static LPWSTR vfmt(LPCWSTR fmt_str, va_list args) {
    LPWSTR buf = NULL;
    DWORD result = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_STRING,
        fmt_str,
        0, 0,
        (LPWSTR)&buf,
        0,
        &args
    );

    va_end(args);

    if (! result) {
        static const WCHAR err[] = L"Failed to format string\n";
        WriteConsole(std_err, err, ARR_SIZE(err), NULL, NULL);

        return NULL;
    }

    return buf;
}

void print_fmt(LPCWSTR fmt_str, ...) {
    va_list args;
    va_start(args, fmt_str);

    LPWSTR msg = vfmt(fmt_str, args);
    va_end(args);

    WriteConsole(std_out, msg, lstrlenW(msg), NULL, NULL);
    LocalFree(msg);
}

void print_err_fmt(LPCWSTR fmt_str, ...) {
    va_list args;
    va_start(args, fmt_str);

    LPWSTR msg = vfmt(fmt_str, args);
    va_end(args);

    WriteConsole(std_err, msg, lstrlenW(msg), NULL, NULL);
    LocalFree(msg);
}

DWORD print_str_fmt(LPWSTR out, DWORD size, LPCWSTR fmt_str, ...) {
    va_list args;
    va_start(args, fmt_str);

    DWORD num_chars = FormatMessage(
        FORMAT_MESSAGE_FROM_STRING,
        fmt_str,
        0, 0,
        out,
        size,
        &args
    );
    va_end(args);

    if (! num_chars) {
        static const WCHAR err[] = L"Failed to format string\n";
        WriteConsole(std_err, err, ARR_SIZE(err), NULL, NULL);

        return -1;
    }

    return num_chars;
}

void check_err(BOOL cond) {
    if (! cond) {
        return;
    }

    DWORD err = GetLastError();
    LPWSTR buf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        err,
        0,
        (LPWSTR)&buf,
        0,
        NULL
    );

    WriteConsole(std_err, buf, lstrlenW(buf), NULL, NULL);
    exit_process(err);
}

void check_hresult(HRESULT code, LPCWSTR err_prefix) {
    if (code == S_OK) {
        return;
    }

    print_err_fmt(L"Fatal error: %1!s! (Code: %2!x!)\n", err_prefix, code);
    exit_process(code);
}

void * alloc_or_die(SIZE_T num_bytes) {
    void * out = HeapAlloc(heap, HEAP_GENERATE_EXCEPTIONS, num_bytes);

    if (! out) {
        print_err_fmt(L"Failed to allocate memory: %1!u!\n", num_bytes);
        exit_process(1);
    }

    return out;
}

void dealloc(void * heap_mem) {
    HeapFree(heap, 0, heap_mem);
}

#pragma function(memcpy)
void * memcpy(void * dest, const void * src, size_t count) {
    for (int i = 0; i < count; i++) {
        ((unsigned char *)dest)[i] = ((const unsigned char *)src)[i];
    }

    return dest;
}

#pragma function(memset)
void * memset(void * dest, int c, size_t count) {
    for (int i = 0; i < count; i++) {
        ((unsigned char *)dest)[i] = (unsigned char)c;
    }

    return dest;
}

#pragma function(memcmp)
int memcmp(const void * ptr1, const void * ptr2, size_t num) {
    for (int i = 0; i < num; i++) {
        unsigned char c1 = ((unsigned char *)ptr1)[i];
        unsigned char c2 = ((unsigned char *)ptr2)[i];

        if (c1 != c2) {
            return c1 - c2;
        }
    }

    return 0;
}

int find_wstr(const wchar_t * str, const wchar_t * substr) {
    int i = 0;
    int sub_i = 0;
    int start = 0;
    wchar_t c;
    wchar_t sub_c;

    while ((c = str[i]) != L'\0') {
        if ((sub_c = substr[sub_i]) == L'\0') {
            return start;
        }

        if (c == sub_c) {
            sub_i++;
        } else {
            sub_i = 0;
            start = i + 1;
        }

        i++;
    }

    if (substr[sub_i] == L'\0') {
        return start;
    }

    return -1;
}

wchar_t * copy_wstr(wchar_t * dest, const wchar_t * src) {
    int i = 0;

    while ((dest[i] = src[i]) != L'\0') {
        i++;
    }

    return dest;
}

int wstr_len(const wchar_t * str) {
    int i = 0;

    while (str[i++]);

    return i - 1;
}

BOOL wstr_eq(const wchar_t * str1, const wchar_t * str2) {
    int i = 0;
    wchar_t c1;
    wchar_t c2;

    while (((c1 = str1[i]) != L'\0') && ((c2 = str2[i]) != L'\0')) {
        if (c1 != c2) {
            return FALSE;
        }

        i++;
    }

    return str1[i] == str2[i];
}

const wchar_t * basename(const wchar_t * path) {
    int len = wstr_len(path);
    int i = len;

    while (i >= 0 && (path[i--] != L'\\'));

    if (path[i + 1] == L'\\') {
        return path + i + 2;
    }

    return path + i + 1;
}

const wchar_t * get_guid_name(const GUID * guid) {
    for (size_t i = 0; i < ARR_SIZE(guid_names); i++) {
        if (IsEqualGUID(guid, guid_names[i].guid)) {
            return guid_names[i].name;
        }
    }

    return NULL;
}

void WINAPI entry() {
    std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    check_err(std_out == INVALID_HANDLE_VALUE);

    std_err = GetStdHandle(STD_ERROR_HANDLE);
    check_err(std_err == INVALID_HANDLE_VALUE);

    heap = GetProcessHeap();
    check_err(! heap);

    LPWSTR cmd_line = GetCommandLine();
    LPWSTR * argv;
    DWORD argc;

    argv = CommandLineToArgvW(cmd_line, &argc);
    check_err(! argv);

    int result = wmain(argc, argv);
    exit_process(result);
}