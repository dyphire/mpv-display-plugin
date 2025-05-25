# mpv-display-plugin

more display properties for mpv on Windows, support toggle Windows HDR

## Features

- Add some information from the displays to the `user-data` subproperties of mpv
- Monitor mpv window changes and displays hot-plug messages to dynamically update the corresponding sub-properties
- Register the script message `toggle-hdr-display` to toggle the HDR state of Windows system

## Installation

[mpv](https://mpv.io) >= `0.37.0` is required, and the `cplugins` feature should be enabled.

Download the plugin from [Releases](https://github.com/tsl0922/mpv-menu-plugin/releases/latest), place the `.dll` file in your mpv [scripts](https://mpv.io/manual/master/#script-location) folder.

> [!TIP]
> To find mpv config location on Windows, run `echo %APPDATA%\mpv` in `cmd.exe`.
>
> You can also use `portable_config` next to `mpv.exe`, read [FILES ON WINDOWS](https://mpv.io/manual/master/#files-on-windows).
>
> If the `scripts` folder doesn't exist in mpv config dir, you may create it yourself.

## subproperties

The plugin provides the following `user-data` sub-properties

### user-data/display-list/full

This property provides information about all displays connected to the Windows system in the form of a JSON string.

- Monitor displays hot-plugging signals dynamic update property

JSON string content structure reference:

```json
[
  {
    "name": "Generic PnP Monitor",
    "uid": "1234",
    "hdr_supported": true,
    "hdr_status": "on",
    "width": 3840,
    "height": 2160,
    "refresh_rate": 60.00,
    "bit_depth": 10,
    "technology": "DisplayPort",
    "current": true
  },
  {
    "name": "Generic PnP Monitor",
    "uid": "567890",
    "hdr_supported": false,
    "hdr_status": "unsupported",
    "width": 1920,
    "height": 1080,
    "refresh_rate": 165.00,
    "bit_depth": 8,
    "technology": "HDMI",
    "current": false
  }
]
```

### user-data/display-list/current

This property provides information in the form of a JSON string about the current display on which the mpv window is located

- Monitor display hot-plugging signals dynamic update property

JSON string content structure reference:

```json
{
  "name": "Generic PnP Monitor",
  "uid": "1234",
  "hdr_supported": true,
  "hdr_status": "on",
  "width": 3840,
  "height": 2160,
  "refresh_rate": 60.00,
  "bit_depth": 10,
  "technology": "DisplayPort",
  "current": true
}
```

### user-data/display-info

This property provides the following sub-properties with information about the monitor on which the mpv window is located

- Monitor display hot-plugging signals dynamic update property

**user-data/display-info/name**

Friendly name of the current monitor

**user-data/display-info/uid**

UID of the current monitor

**user-data/display-info/hdr-supported**

HDR support for current monitor (true/false)

**user-data/display-info/hdr-supported**

HDR status of current monitor. Possible values: on/off/unsupported

**user-data/display-info/refresh-rate**

Refresh rate of the current monitor

**user-data/display-info/bit-depth**

Bit depth of the current monitor. Possible values: 6/8/10/12

## Script message

The plugin registers a script message `toggle-hdr-display` to toggle the HDR state of the display on which the mpv window is located

### Usage

Add the appropriate key bindings to `input.conf`:

**toggle hdr**

```
key  script-message toggle-hdr-display
```

**enable hdr**

```
key  script-message toggle-hdr-display on
```

**disable hdr**

```
key  script-message toggle-hdr-display off
```

## Related Scripts

- [hdr-mode.lua](https://github.com/dyphire/mpv-scripts/blob/main/hdr-mode.lua "hdr-mode.lua")
