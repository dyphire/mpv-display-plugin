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
        "current": true,
        "hdr_supported": true,
        "hdr_status": "on",
        "width": 3840,
        "height": 2160,
        "refresh_rate": 60.00,
        "bit_depth": 10,
        "primaries": "BT.2020",
        "transfer": "PQ",
        "max_luminance": 1107.00,
        "min_luminance": 0.0108,
        "max_full_frame_luminance": 972.0000,
        "technology": "DisplayPort"
    },
    {
        "name": "Unknown",
        "uid": "567890",
        "current": false,
        "hdr_supported": false,
        "hdr_status": "unsupported",
        "width": 2560,
        "height": 1440,
        "refresh_rate": 165.00,
        "bit_depth": 8,
        "primaries": "BT.709",
        "transfer": "sRGB",
        "max_luminance": 270.00,
        "min_luminance": 0.5000,
        "max_full_frame_luminance": 270.0000,
        "technology": "Internal"
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
    "current": true,
    "hdr_supported": true,
    "hdr_status": "on",
    "width": 3840,
    "height": 2160,
    "refresh_rate": 60.00,
    "bit_depth": 10,
    "primaries": "BT.2020",
    "transfer": "PQ",
    "max_luminance": 1107.00,
    "min_luminance": 0.0108,
    "max_full_frame_luminance": 972.0000,
    "technology": "DisplayPort"
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

**user-data/display-info/hdr-status**

HDR status of current monitor. Possible values: on/off/unsupported

**user-data/display-info/refresh-rate**

Refresh rate of the current monitor

**user-data/display-info/bit-depth**

Bit depth of the current monitor. Possible values: 6/8/10/12

**user-data/display-info/primaries**

The color space of the current monitor. Possible values: BT.709/BT.2020

> [!NOTE]
> Not always accurate, apparently Windows systems report incorrect information

**user-data/display-info/transfer**

Transmission characteristics of current displays. Possible values: sRGB/Linear/PQ

**user-data/display-info/max-luminance**

The maximum luminance, in nits, that the current display attached to this output is capable of rendering;
this value is likely only valid for a small area of the panel.

**user-data/display-info/min-luminance**

The minimum luminance, in nits, that the current display attached to this output is capable of rendering.

**user-data/display-info/max-full-frame-luminance**

The maximum luminance, in nits, that the display attached to this output is capable of rendering
unlike MaxLuminance, this value is valid for a color that fills the entire area of the panel.
Content should not exceed this value across the entire panel for optimal rendering.

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
