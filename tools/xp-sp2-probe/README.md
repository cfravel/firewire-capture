# XP SP2 DirectShow Probe

This is an isolated diagnostic utility for inspecting the DirectShow devices,
pins, media types, and optional transport interfaces exposed by the XP SP2
FireWire stack.

It is deliberately not part of `firewire-capture.sln` and does not include or
link any production source files. Its output and intermediate files are kept
under this directory.

## Current scope

The probe:

1. initializes COM;
2. enumerates `CLSID_VideoInputDeviceCategory` devices;
3. prints FriendlyName, Description, DevicePath, and CLSID;
4. binds each device as an `IBaseFilter`;
5. reports `IAMExtTransport` and `IAMTimecodeReader` availability;
6. lists every pin and its media types.

It does not start a graph, issue PLAY or STOP, write capture files, or decode
video. This keeps the first XP test focused on device exposure and media
negotiation.

## Build status

The checked-in project is configured for an isolated x86 Release build with an
XP API floor, static runtime, and a 5.01 console subsystem version. The current
development machine only has the Visual Studio 2026 `v145` toolset installed.

Therefore, an executable built here is not an XP SP2 binary. The current
`v145` static runtime imports `InitializeCriticalSectionEx`, which is not
available on XP SP2. Do not transfer the locally built executable to the
laptop. Rebuild this project with an older compiler/toolchain known to support
XP SP2, inspect its imports, and then test it on the offline XP machine.

Do not add this project to the production solution and do not change the
production project's toolset to build it.

## Build from this directory

```cmd
msbuild xp-sp2-probe.vcxproj /m /p:Configuration=Release /p:Platform=Win32
```

The output is:

```text
tools\xp-sp2-probe\bin\Release\xp-sp2-probe.exe
```

Run the probe with the camcorder connected and powered on in VCR/playback mode.
