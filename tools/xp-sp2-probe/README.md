# XP SP2 DirectShow Probe

This is an isolated diagnostic utility for inspecting and briefly capturing
from DirectShow devices, pins, media types, and optional transport interfaces
exposed by the XP SP2 FireWire stack.

It is deliberately not part of `firewire-capture.sln` and does not include or
link any production source files. Its output and intermediate files are kept
under this directory.

## Current scope

With no output argument, the probe:

1. initializes COM;
2. enumerates `CLSID_VideoInputDeviceCategory` devices;
3. prints FriendlyName, Description, DevicePath, and CLSID;
4. binds each device as an `IBaseFilter`;
5. reports `IAMExtTransport` and `IAMTimecodeReader` availability;
6. lists every pin and its media types.

With one output argument, the probe instead selects the first `DV A/V Out` or
`MPEG2TS Out` source, connects it to an isolated native sample-writing sink,
starts the graph, issues PLAY, writes native samples for ten seconds, issues
STOP, and reports sample and byte counts.

The writing mode does not decode, transcode, or preview video. It is a short
graph and disk-I/O test, not yet a full replacement for `fwcap.exe`.

## Build status

The checked-in project is configured for an isolated x86 Release build with an
XP API floor, no linked C/C++ runtime, baseline IA-32 instructions, and a 5.01
console subsystem version. The IA-32 instruction floor is required for older
Pentium III-class XP machines that do not support SSE2.
The current development machine only has the Visual Studio 2026 `v145`
toolset installed.

The probe uses a custom entry point and small Win32 output/runtime helpers so
the resulting PE does not import the modern MSVC CRT. The locally inspected
binary imports only XP-era functions from `KERNEL32`, `ole32`, and `OLEAUT32`,
and has x86 PE and subsystem version 5.01. This is a strong compatibility
signal, but it is not a substitute for running the probe on XP SP2. Transfer
the Release executable only after reviewing the imports and keep the XP test
machine offline.

The probe's DirectShow device enumeration still depends on the XP system's
registered DirectShow components and FireWire driver stack.

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

Run enumeration mode with no argument:

```cmd
xp-sp2-probe.exe
```

Run a ten-second native capture test with an output path:

```cmd
xp-sp2-probe.exe test.dv
xp-sp2-probe.exe test.m2t
```

Use `test.dv` while the camcorder is in DV mode and `test.m2t` while it is in
HDV mode. The extension is supplied by the operator; the probe selects the
native stream based on the DirectShow output pin.
