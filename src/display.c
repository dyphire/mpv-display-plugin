// Copyright (c) 2023 dyphire. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <mpv/client.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#define INITGUID
#include <dxgi1_6.h> 
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#define MPV_EXPORT __declspec(dllexport)
#define SAFE_RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while(0)

static mpv_handle *mpv = NULL;
static HWND hwnd = NULL;
static HWND message_hwnd = NULL;
static const char *CLASS_NAME = "MPVDisplayMonitorWindow";

typedef enum {
    HDR_STATUS_UNSUPPORTED,
    HDR_STATUS_OFF,
    HDR_STATUS_ON
} HDR_STATUS;

BOOL IsWindows11_24H2OrGreater() {
    OSVERSIONINFOEXW osvi = { sizeof(osvi), 10, 0, 0, 0, L"", 0, 0 };
    DWORDLONG mask = VerSetConditionMask(
        VerSetConditionMask(
            VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL),
            VER_MINORVERSION, VER_EQUAL),
        VER_BUILDNUMBER, VER_GREATER_EQUAL);

    osvi.dwBuildNumber = 26100; // Windows 11 24H2 build
    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, mask);
}

static void mpv_print(const char *fmt, ...) {
    #ifdef DEBUG
        if (!mpv) return;
    
        wchar_t wbuf[512];
        va_list args;
        va_start(args, fmt);
        vswprintf(wbuf, sizeof(wbuf) / sizeof(wchar_t), fmt, args);
        va_end(args);

        char utf8buf[600];
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, utf8buf, sizeof(utf8buf), NULL, NULL);
    
        char cmd[700];
        snprintf(cmd, sizeof(cmd), "print-text \"[display-info] %s\"", utf8buf);
    
        mpv_command_string(mpv, cmd);
    #endif
}

static const char *hdr_status_to_str(HDR_STATUS s) {
    switch (s) {
        case HDR_STATUS_OFF: return "off";
        case HDR_STATUS_ON: return "on";
        case HDR_STATUS_UNSUPPORTED: return "unsupported";
        default: return "unknown";
    }
}

static HMONITOR GetWindowMonitor(HWND hwnd) {
    return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
}

static bool GetDisplayConfigForMonitor(HMONITOR hMon, DISPLAYCONFIG_MODE_INFO *outMode) {
    MONITORINFOEX monInfo = { .cbSize = sizeof(monInfo) };
    if (!GetMonitorInfo(hMon, (MONITORINFO*)&monInfo)) {
        mpv_print("GetMonitorInfo failed");
        return false;
    }

    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        mpv_print("GetDisplayConfigBufferSizes failed");
        return false;
    }

    DISPLAYCONFIG_PATH_INFO *paths = calloc(pathCount, sizeof(*paths));
    DISPLAYCONFIG_MODE_INFO *modes = calloc(modeCount, sizeof(*modes));
    if (!paths || !modes) {
        mpv_print("Memory allocation failed");
        free(paths);
        free(modes);
        return false;
    }

    bool found = false;

    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths, &modeCount, modes, NULL) == ERROR_SUCCESS) {
        for (UINT32 i = 0; i < pathCount; i++) {
            DISPLAYCONFIG_PATH_INFO *path = &paths[i];

            DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {
                .header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                .header.size = sizeof(sourceName),
                .header.adapterId = path->sourceInfo.adapterId,
                .header.id = path->sourceInfo.id
            };

            if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
                wchar_t szDeviceW[32];
                MultiByteToWideChar(CP_ACP, 0, monInfo.szDevice, -1, szDeviceW, 32);

                wchar_t *viewName = sourceName.viewGdiDeviceName;

                //mpv_print("szDevice: %S, sourceName: %S", szDeviceW, viewName);

                if (wcscmp(szDeviceW, viewName) == 0) {
                    for (UINT32 j = 0; j < modeCount; j++) {
                        if (modes[j].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET &&
                            modes[j].id == path->targetInfo.id &&
                            modes[j].adapterId.HighPart == path->targetInfo.adapterId.HighPart &&
                            modes[j].adapterId.LowPart == path->targetInfo.adapterId.LowPart) {
                            *outMode = modes[j];
                            found = true;
                            mpv_print("Matching display config found");
                            break;
                        }
                    }
                }
            }
            if (found) break;
        }
    }

    free(paths);
    free(modes);

    if (!found)
        mpv_print("No matching display config found");

    return found;
}

static HDR_STATUS GetDisplayHDRStatusAndBitDepth(const DISPLAYCONFIG_MODE_INFO *mode, UINT32 *outBitDepth) {
    if (outBitDepth)
        *outBitDepth = 8;

    if (IsWindows11_24H2OrGreater()) {
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 ColorInfo2 = { 0 };
        ColorInfo2.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2;
        ColorInfo2.header.size = sizeof(ColorInfo2);
        ColorInfo2.header.adapterId = mode->adapterId;
        ColorInfo2.header.id = mode->id;

        if (DisplayConfigGetDeviceInfo(&ColorInfo2.header) != ERROR_SUCCESS) {
            mpv_print("Get HDR status failed");
            return HDR_STATUS_UNSUPPORTED;
        }

        if (outBitDepth)
            *outBitDepth = ColorInfo2.bitsPerColorChannel;
    
        if (!ColorInfo2.highDynamicRangeSupported)
            return HDR_STATUS_UNSUPPORTED;
    
        return ColorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR ? HDR_STATUS_ON : HDR_STATUS_OFF;

    } else {
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO ColorInfo = { 0 };
        ColorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        ColorInfo.header.size = sizeof(ColorInfo);
        ColorInfo.header.adapterId = mode->adapterId;
        ColorInfo.header.id = mode->id;

        if (DisplayConfigGetDeviceInfo(&ColorInfo.header) != ERROR_SUCCESS) {
            mpv_print("Get HDR status failed");
            return HDR_STATUS_UNSUPPORTED;
        }

        if (outBitDepth)
            *outBitDepth = ColorInfo.bitsPerColorChannel;
    
        if (!ColorInfo.advancedColorSupported)
            return HDR_STATUS_UNSUPPORTED;
    
        return (ColorInfo.advancedColorEnabled && !ColorInfo.wideColorEnforced) ? HDR_STATUS_ON : HDR_STATUS_OFF;
    }
}

static bool SetDisplayHDRStatus(const DISPLAYCONFIG_MODE_INFO *mode, bool enable, HDR_STATUS *out) {
    mpv_print("Setting HDR to %s...", enable ? "on" : "off");

    if (IsWindows11_24H2OrGreater()) {
        DISPLAYCONFIG_SET_HDR_STATE setHdrState = {0};
        setHdrState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE;
        setHdrState.header.size = sizeof(setHdrState);
        setHdrState.header.adapterId = mode->adapterId;
        setHdrState.header.id = mode->id;
        setHdrState.enableHdr = enable;
    
        if (DisplayConfigSetDeviceInfo(&setHdrState.header) != ERROR_SUCCESS) {
            mpv_print("Failed to set HDR");
            return false;
        }
    
        UINT32 bit_depth;
        *out = GetDisplayHDRStatusAndBitDepth(mode, &bit_depth);
        return true;
    } else {
        DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setColorState = {0};
        setColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
        setColorState.header.size = sizeof(setColorState);
        setColorState.header.adapterId = mode->adapterId;
        setColorState.header.id = mode->id;
        setColorState.enableAdvancedColor = enable;
    
        if (DisplayConfigSetDeviceInfo(&setColorState.header) != ERROR_SUCCESS) {
            mpv_print("Failed to set HDR");
            return false;
        }
    
        UINT32 bit_depth;
        *out = GetDisplayHDRStatusAndBitDepth(mode, &bit_depth);
        return true;
    }
    
}

static void GetMonitorName(const DISPLAYCONFIG_MODE_INFO *mode, char *out, size_t outlen) {
    DISPLAYCONFIG_TARGET_DEVICE_NAME nameInfo = {0};
    nameInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    nameInfo.header.size = sizeof(nameInfo);
    nameInfo.header.adapterId = mode->adapterId;
    nameInfo.header.id = mode->id;

    if (DisplayConfigGetDeviceInfo(&nameInfo.header) == ERROR_SUCCESS) {
        WideCharToMultiByte(CP_UTF8, 0, nameInfo.monitorFriendlyDeviceName, -1, out, (int)outlen, NULL, NULL);
    } else {
        snprintf(out, outlen, "Unknown");
    }
}

static HMONITOR get_hm_from_display_path(const DISPLAYCONFIG_PATH_INFO* path, DISPLAYCONFIG_MODE_INFO* modes, UINT32 modeCount) {
    DISPLAYCONFIG_SOURCE_MODE* src_mode = NULL;
    for (UINT32 k = 0; k < modeCount; k++) {
        if (modes[k].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE &&
            modes[k].id == path->sourceInfo.id &&
            modes[k].adapterId.HighPart == path->sourceInfo.adapterId.HighPart &&
            modes[k].adapterId.LowPart == path->sourceInfo.adapterId.LowPart) {
            src_mode = &modes[k].sourceMode;
            break;
        }
    }
    if (!src_mode) return NULL;

    RECT monitor_rect = {
        .left   = src_mode->position.x,
        .top    = src_mode->position.y,
        .right  = src_mode->position.x + src_mode->width,
        .bottom = src_mode->position.y + src_mode->height
    };

    return MonitorFromRect(&monitor_rect, MONITOR_DEFAULTTONULL);
}

// Helper function to convert DXGI_COLOR_SPACE_TYPE to string for primaries
static const char *dxgi_primaries_to_str_local(DXGI_COLOR_SPACE_TYPE colorSpace) {
    switch (colorSpace) {
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
        // case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709_SL: // This specific SL enum doesn't exist in standard dxgitype.h
            return "BT.709";
        case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: // Linear gamma BT.709
            return "BT.709"; // Primaries are still BT.709
        case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
            return "BT.2020";
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020: // BT.2020 primaries with gamma 2.2
             return "BT.2020";
        default:
            mpv_print("Unknown DXGI ColorSpace for primaries: %d", colorSpace);
            return "Unknown";
    }
}
 
// Helper function to convert DXGI_COLOR_SPACE_TYPE to string for transfer characteristics
static const char *dxgi_transfer_to_str_local(DXGI_COLOR_SPACE_TYPE colorSpace) {
    switch (colorSpace) {
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
        // case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709_SL:
            return "sRGB"; // Explicitly G2.2, could also be sRGB or BT.1886 depending on context
        case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
            return "Linear";
        case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
            return "PQ";
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
             return "sRGB";
        default:
            mpv_print("Unknown DXGI ColorSpace for transfer: %d", colorSpace);
            return "Unknown";
    }
}

static IDXGIFactory1* g_dxgiFactory = NULL;

// New function to get DXGI_OUTPUT_DESC1 for a specific HMONITOR
static bool get_dxgi_output_desc1_for_monitor(HMONITOR hMon, DXGI_OUTPUT_DESC1 *out_desc) {
    if (!hMon || !out_desc) return false;

    IDXGIFactory1 *dxgiFactory = NULL;
    HRESULT hr;

    // CreateDXGIFactory1 is a global function, so its call style is correct.
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&dxgiFactory);
    if (FAILED(hr) || !dxgiFactory) {
        mpv_print("Failed to create DXGI Factory: 0x%lX", hr);
        return false;
    }

    bool found_match = false;
    for (UINT i = 0; ; ++i) { // Adapter loop
        IDXGIAdapter1 *adapter = NULL;
        // CORRECTED CALL:
        hr = dxgiFactory->lpVtbl->EnumAdapters1(dxgiFactory, i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break; // No more adapters
        if (FAILED(hr) || !adapter) {
            mpv_print("Error enumerating DXGI adapter %u: 0x%lX", i, hr);
            continue;
        }

        for (UINT j = 0; ; ++j) { // Output loop
            IDXGIOutput *output = NULL;
            // CORRECTED CALL:
            hr = adapter->lpVtbl->EnumOutputs(adapter, j, &output);
            if (hr == DXGI_ERROR_NOT_FOUND) break; // No more outputs for this adapter
            if (FAILED(hr) || !output) {
                mpv_print("Error enumerating DXGI output %u on adapter %u: 0x%lX", j, i, hr);
                continue;
            }

            DXGI_OUTPUT_DESC desc;
            // CORRECTED CALL:
            hr = output->lpVtbl->GetDesc(output, &desc);
            if (SUCCEEDED(hr) && desc.Monitor == hMon) {
                IDXGIOutput6 *output6 = NULL;
                // CORRECTED CALL:
                hr = output->lpVtbl->QueryInterface(output, &IID_IDXGIOutput6, (void **)&output6);
                if (SUCCEEDED(hr) && output6) {
                    // CORRECTED CALL:
                    hr = output6->lpVtbl->GetDesc1(output6, out_desc);
                    if (SUCCEEDED(hr)) {
                        found_match = true;
                    } else {
                        mpv_print("IDXGIOutput6_GetDesc1 failed: 0x%lX", hr);
                    }
                    SAFE_RELEASE(output6); // SAFE_RELEASE already uses ->lpVtbl->Release
                } else {
                    mpv_print("QueryInterface for IDXGIOutput6 failed or IDXGIOutput6 not supported (0x%lX).", hr);
                }
            }
            SAFE_RELEASE(output);
            if (found_match) break;
        }
        SAFE_RELEASE(adapter);
        if (found_match) break;
    }

    SAFE_RELEASE(dxgiFactory);

    return found_match;
}

static void update_display_list() {
    HMONITOR current_monitor = GetWindowMonitor(hwnd);

    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return;

    DISPLAYCONFIG_PATH_INFO *paths = calloc(pathCount, sizeof(*paths));
    DISPLAYCONFIG_MODE_INFO *modes = calloc(modeCount, sizeof(*modes));
    if (!paths || !modes) {
        free(paths);
        free(modes);
        return;
    }

    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths, &modeCount, modes, NULL) != ERROR_SUCCESS) {
        free(paths);
        free(modes);
        return;
    }

    char json[8192];
    size_t json_len = 0;
    json[0] = '[';
    json_len = 1;

    char current_json[1024];
    strcpy_s(current_json, sizeof(current_json), "{}");

    for (UINT32 i = 0; i < pathCount; i++) {
        DISPLAYCONFIG_PATH_INFO *path = &paths[i];

        HMONITOR hMonitor = get_hm_from_display_path(path, modes, modeCount);
        if (!hMonitor) continue;

        for (UINT32 j = 0; j < modeCount; j++) {
            if (modes[j].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET &&
                modes[j].id == path->targetInfo.id &&
                modes[j].adapterId.HighPart == path->targetInfo.adapterId.HighPart &&
                modes[j].adapterId.LowPart == path->targetInfo.adapterId.LowPart) {

                DISPLAYCONFIG_MODE_INFO *mode = &modes[j];

                char uid[64];
                snprintf(uid, sizeof(uid), "%u", mode->id);

                char name[128] = "Unknown";
                GetMonitorName(mode, name, sizeof(name));
                if (name[0] == '\0')
                    snprintf(name, sizeof(name), "Unknown");

                UINT32 bitDepth = 0;
                HDR_STATUS status = GetDisplayHDRStatusAndBitDepth(mode, &bitDepth);

                UINT32 width = 0, height = 0;
                float refresh = 0.0f;
                for (UINT32 k = 0; k < modeCount; k++) {
                    if (modes[k].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE &&
                        modes[k].id == path->sourceInfo.id &&
                        modes[k].adapterId.HighPart == path->sourceInfo.adapterId.HighPart &&
                        modes[k].adapterId.LowPart == path->sourceInfo.adapterId.LowPart) {
                        DISPLAYCONFIG_SOURCE_MODE *src = &modes[k].sourceMode;
                        width = src->width;
                        height = src->height;
                        if (path->targetInfo.refreshRate.Denominator != 0)
                            refresh = path->targetInfo.refreshRate.Numerator / (float)path->targetInfo.refreshRate.Denominator;
                        break;
                    }
                }

                const char *tech = "Unknown";
                switch (path->targetInfo.outputTechnology) {
                    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI: tech = "HDMI"; break;
                    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL: tech = "DisplayPort"; break;
                    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED: tech = "eDP"; break;
                    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI: tech = "DVI"; break;
                    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL: tech = "Internal"; break;
                    default: break;
                }

                float max_luminance_val = 0.0f;
                float min_luminance_val = 0.0f;
                float max_full_frame_luminance_val = 0.0f;
                const char *dxgi_primaries_str = "Unknown";
                const char *dxgi_transfer_str = "Unknown";
                DXGI_OUTPUT_DESC1 dxgi_desc1;
                if (get_dxgi_output_desc1_for_monitor(hMonitor, &dxgi_desc1)) {
                    max_luminance_val = dxgi_desc1.MaxLuminance;
                    min_luminance_val = dxgi_desc1.MinLuminance;
                    max_full_frame_luminance_val = dxgi_desc1.MaxFullFrameLuminance;
                    dxgi_primaries_str = dxgi_primaries_to_str_local(dxgi_desc1.ColorSpace);
                    dxgi_transfer_str = dxgi_transfer_to_str_local(dxgi_desc1.ColorSpace);
                    mpv_print("DXGI Info: MaxL:%.2f, MinL:%.4f, Prim:%s, Trans:%s",
                              max_luminance_val, min_luminance_val, dxgi_primaries_str, dxgi_transfer_str);
                } else {
                    mpv_print("Failed to get DXGI_OUTPUT_DESC1 for monitor.");
                }

                bool is_current = false;
                MONITORINFOEX monInfo = { .cbSize = sizeof(monInfo) };
                if (GetMonitorInfo(current_monitor, (MONITORINFO*)&monInfo)) {
                    DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {
                        .header = {
                            .type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                            .size = sizeof(sourceName),
                            .adapterId = path->sourceInfo.adapterId,
                            .id = path->sourceInfo.id
                        }
                    };
                    if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
                        wchar_t szDeviceW[32];
                        MultiByteToWideChar(CP_ACP, 0, monInfo.szDevice, -1, szDeviceW, 32);
                        if (wcscmp(szDeviceW, sourceName.viewGdiDeviceName) == 0) {
                            is_current = true;

                            snprintf(current_json, sizeof(current_json),
                                "{\"name\":\"%s\",\"uid\":\"%s\",\"current\":true,\"hdr_supported\":%s,\"hdr_status\":\"%s\","
                                "\"width\":%u,\"height\":%u,\"refresh_rate\":%.2f,\"bit_depth\":%u,"
                                "\"primaries\":\"%s\",\"transfer\":\"%s\","
                                "\"max_luminance\":%.2f,\"min_luminance\":%.4f,\"max_full_frame_luminance\":%.4f,"
                                "\"technology\":\"%s\"}",
                                name, uid,
                                (status == HDR_STATUS_UNSUPPORTED ? "false" : "true"),
                                hdr_status_to_str(status),
                                width, height, refresh, bitDepth,
                                dxgi_primaries_str, dxgi_transfer_str,
                                max_luminance_val, min_luminance_val, max_full_frame_luminance_val,
                                tech);

                            mpv_set_property_string(mpv, "user-data/display-info/name", name);
                            mpv_set_property_string(mpv, "user-data/display-info/uid", uid);
                            mpv_set_property_string(mpv, "user-data/display-info/hdr-supported", (status == HDR_STATUS_UNSUPPORTED) ? "false" : "true");
                            mpv_set_property_string(mpv, "user-data/display-info/hdr-status", hdr_status_to_str(status));

                            char temp_str[32];
                            snprintf(temp_str, sizeof(temp_str), "%u", bitDepth);
                            mpv_set_property_string(mpv, "user-data/display-info/bit-depth", temp_str);
                            snprintf(temp_str, sizeof(temp_str), "%.2f", refresh);
                            mpv_set_property_string(mpv, "user-data/display-info/refresh-rate", temp_str);
                            snprintf(temp_str, sizeof(temp_str), "%.2f", max_luminance_val);
                            mpv_set_property_string(mpv, "user-data/display-info/max-luminance", temp_str);
                            snprintf(temp_str, sizeof(temp_str), "%.4f", min_luminance_val);
                            mpv_set_property_string(mpv, "user-data/display-info/min-luminance", temp_str);
                            snprintf(temp_str, sizeof(temp_str), "%.4f", max_full_frame_luminance_val);
                            mpv_set_property_string(mpv, "user-data/display-info/max-full-frame-luminance", temp_str);
                            mpv_set_property_string(mpv, "user-data/display-info/primaries", dxgi_primaries_str);
                            mpv_set_property_string(mpv, "user-data/display-info/transfer", dxgi_transfer_str);

                            mpv_print("Display: %s, HDR: %s", name, hdr_status_to_str(status));
                        }
                    }
                }

                if (json_len < sizeof(json) - 1) {
                    if (json_len > 1) {
                        json[json_len++] = ',';
                        json[json_len] = '\0';
                    }
                    int written = snprintf(json + json_len, sizeof(json) - json_len,
                        "{\"name\":\"%s\",\"uid\":\"%s\",\"current\":%s, \"hdr_supported\":%s,\"hdr_status\":\"%s\","
                        "\"width\":%u,\"height\":%u,\"refresh_rate\":%.2f,\"bit_depth\":%d,"
                        "\"primaries\":\"%s\",\"transfer\":\"%s\","
                        "\"max_luminance\":%.2f,\"min_luminance\":%.4f,\"max_full_frame_luminance\":%.4f,"
                        "\"technology\":\"%s\"}",
                        name, uid, is_current ? "true" : "false",
                        (status == HDR_STATUS_UNSUPPORTED ? "false" : "true"),
                        hdr_status_to_str(status),
                        width, height, refresh, bitDepth,
                        dxgi_primaries_str, dxgi_transfer_str,
                        max_luminance_val, min_luminance_val, max_full_frame_luminance_val,
                        tech);
                    if (written > 0 && (size_t)written < sizeof(json) - json_len) {
                        json_len += (size_t)written;
                    } else {
                        mpv_print("Warning: json buffer overflow");
                        break;
                    }
                }

                break;
            }
        }
    }

    if (json_len < sizeof(json) - 1) {
        json[json_len++] = ']';
        json[json_len] = '\0';
    } else {
        mpv_print("Warning: json buffer full, appending closing bracket failed");
        if (json_len > 0)
            json[json_len - 1] = ']';
    }

    mpv_set_property_string(mpv, "user-data/display-list/full", json);
    mpv_set_property_string(mpv, "user-data/display-list/current", current_json);

    free(paths);
    free(modes);
}

static void update_mpv_properties() {
    mpv_print("Updating display properties...");

    DISPLAYCONFIG_MODE_INFO mode;
    if (!GetDisplayConfigForMonitor(GetWindowMonitor(hwnd), &mode)) {
        mpv_print("Failed to get display mode");
        return;
    }

    update_display_list();
}

static void plugin_init(int64_t wid) {
    hwnd = (HWND)(uintptr_t)wid;
    mpv_print("Plugin initialized");
    update_mpv_properties();
}

static void handle_property_change(mpv_event *event) {
    mpv_event_property *prop = event->data;
    if (prop->format == MPV_FORMAT_INT64 &&
        strcmp(prop->name, "window-id") == 0) {
        int64_t wid = *(int64_t *)prop->data;
        if (wid > 0) plugin_init(*(int64_t *)prop->data);
    }

    if (prop->format == MPV_FORMAT_NODE &&
        strcmp(prop->name, "display-names") == 0) {
        mpv_print("Display names changed");
        update_mpv_properties();
    }
}

static void handle_client_message(mpv_event *event) {
    mpv_event_client_message *msg = event->data;
    if (msg->num_args < 1) return;

    const char *cmd = msg->args[0];
    if (strcmp(cmd, "toggle-hdr-display") != 0) return;

    mpv_print("Received toggle-hdr-display message\n");

    int set_status = -1; // -1: toggle, 0: off, 1: on
    if (msg->num_args >= 2) {
        const char *arg = msg->args[1];
        if (strcmp(arg, "on") == 0) {
            set_status = 1;
        } else if (strcmp(arg, "off") == 0) {
            set_status = 0;
        } else {
            mpv_command_string(mpv, "print-text \"[display-info] Invalid argument. Use: toggle-hdr-display [on|off]\"");
            return;
        }
    }

    DISPLAYCONFIG_MODE_INFO mode;
    if (!GetDisplayConfigForMonitor(GetWindowMonitor(hwnd), &mode)) {
        mpv_command_string(mpv, "print-text \"[display-info] Failed to get display mode for toggle\"");
        return;
    }

    UINT32 bit_depth = 0;
    HDR_STATUS current = GetDisplayHDRStatusAndBitDepth(&mode, &bit_depth);
    if (current == HDR_STATUS_UNSUPPORTED) {
        mpv_command_string(mpv, "print-text \"[display-info] HDR unsupported, cannot toggle\"");
        return;
    }

    bool target_on = (set_status == -1) ? (current != HDR_STATUS_ON) : (set_status == 1);

    HDR_STATUS new_status;
    if (SetDisplayHDRStatus(&mode, target_on, &new_status)) {
        update_mpv_properties();

        char msg[128];
        snprintf(msg, sizeof(msg), "print-text \"[display-info] HDR %s\"", 
                 new_status == HDR_STATUS_ON ? "enabled" : "disabled");
        mpv_command_string(mpv, msg);
    } else {
        mpv_command_string(mpv, "print-text \"[display-info] Failed to change HDR status\"");
    }
}

static LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DISPLAYCHANGE) {
        mpv_print("Received WM_DISPLAYCHANGE: updating display info...");
        update_mpv_properties();
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static void create_message_window() {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = MessageWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    message_hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        "",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    ShowWindow(message_hwnd, SW_HIDE);
}

static DWORD WINAPI MessageThreadProc(LPVOID lpParam) {
    create_message_window();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

MPV_EXPORT int mpv_open_cplugin(mpv_handle *handle) {
    mpv = handle;
    mpv_observe_property(mpv, 0, "window-id", MPV_FORMAT_INT64);
    mpv_observe_property(mpv, 0, "display-names", MPV_FORMAT_NODE);
    mpv_request_event(mpv, MPV_EVENT_CLIENT_MESSAGE, 1);

    CreateThread(NULL, 0, MessageThreadProc, NULL, 0, NULL);

    mpv_print("Plugin loaded and waiting for events...");

    while (mpv) {
        mpv_event *event = mpv_wait_event(mpv, -1);
        if (event->event_id == MPV_EVENT_SHUTDOWN) break;

        switch (event->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE:
                handle_property_change(event);
                break;
            case MPV_EVENT_CLIENT_MESSAGE:
                handle_client_message(event);
                break;
            default:
                break;
        }
    }

    mpv_print("Plugin shutting down");
    mpv_unobserve_property(mpv, 0);
    return 0;
}