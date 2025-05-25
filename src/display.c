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

#define MPV_EXPORT __declspec(dllexport)

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
                                "{\"name\":\"%s\",\"uid\":\"%s\",\"hdr_supported\":%s,\"hdr_status\":\"%s\","
                                "\"width\":%u,\"height\":%u,\"refresh_rate\":%.2f,\"bit_depth\":%d,"
                                "\"technology\":\"%s\",\"current\":true}",
                                name, uid,
                                (status == HDR_STATUS_UNSUPPORTED ? "false" : "true"),
                                hdr_status_to_str(status),
                                width, height, refresh, bitDepth,
                                tech);

                            mpv_set_property_string(mpv, "user-data/display-info/name", name);
                            mpv_set_property_string(mpv, "user-data/display-info/uid", uid);
                            mpv_set_property_string(mpv, "user-data/display-info/hdr-supported", (status == HDR_STATUS_UNSUPPORTED) ? "false" : "true");
                            mpv_set_property_string(mpv, "user-data/display-info/hdr-status", hdr_status_to_str(status));

                            char bitdepth_str[16];
                            snprintf(bitdepth_str, sizeof(bitdepth_str), "%d", bitDepth);
                            mpv_set_property_string(mpv, "user-data/display-info/bit-depth", bitdepth_str);

                            char refresh_str[32];
                            snprintf(refresh_str, sizeof(refresh_str), "%.2f", refresh);
                            mpv_set_property_string(mpv, "user-data/display-info/refresh-rate", refresh_str);

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
                        "{\"name\":\"%s\",\"uid\":\"%s\",\"hdr_supported\":%s,\"hdr_status\":\"%s\","
                        "\"width\":%u,\"height\":%u,\"refresh_rate\":%.2f,\"bit_depth\":%d,"
                        "\"technology\":\"%s\",\"current\":%s}",
                        name, uid,
                        (status == HDR_STATUS_UNSUPPORTED ? "false" : "true"),
                        hdr_status_to_str(status),
                        width, height, refresh, bitDepth,
                        tech,
                        is_current ? "true" : "false");
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

    while (handle) {
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
    return 0;
}