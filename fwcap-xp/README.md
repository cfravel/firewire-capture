# fwcap-xp

This is the isolated Windows XP SP2 x86 edition of `firewire-capture`. It is
deliberately not part of `firewire-capture.sln` and does not use the modern
`fwcap.exe` project's runtime settings, libraries, or build-output directory.

The target combines the XP-compatible native capture implementation in
`fwcap-xp/main.cpp` with the shared GUI in `..\gui\main.cpp`. It produces one
dual-mode executable:

```text
fwcap-xp\bin\Release\fwcap-xp.exe
```

Run `fwcap-xp.exe` with no parameters, or double-click it, to open the GUI. Run
it with a capture name to use the headless command-line interface:

```cmd
fwcap-xp.exe [-v] [--overwrite] [--hdv-discard] <capture-name>
```

The source selects `DV A/V Out` or `MPEG2TS Out` from the connected camcorder,
normalizes the output extension to `.dv` or `.m2t`, writes native samples, and
reports transport state, timecode when valid, elapsed time, sample count, and
byte count. `-v` enables additional diagnostics. `--overwrite` permits an
existing final capture to be replaced and an existing partial capture to be
deleted. `--hdv-discard` is a diagnostic HDV-only mode that accepts and reports
HDV samples without writing a file.

During a normal capture, output is first written as `<name>.dv.partial` or
`<name>.m2t.partial`. A clean stop renames it to the final output name. If the
program or computer crashes, remove only the final `.partial` extension to
recover the native data that had already been written. If the final filename
already exists, preserve it and give the recovered partial a different `.dv` or
`.m2t` name, or move the old final file before renaming the partial.

When switching the camcorder between DV and HDV, power the camcorder off,
disconnect and reconnect FireWire, and then power it back on in VCR/playback
mode. Preview during capture is unsupported and must remain off; use the
camcorder display. If the preview checkbox remains enabled during capture, do
not select it. HDV preview is not supported. See the root `README.md` for
complete user and hardware instructions.

The XP target intentionally does not share the modern C++ runtime or source
build settings. It is compiled for baseline IA-32 instructions and an XP 5.01
PE/subsystem while still sharing the Win32 GUI source.

## Build

```cmd
msbuild fwcap-xp.vcxproj /m /p:Configuration=Release /p:Platform=Win32
```

Output and intermediate files stay below this directory:

```text
fwcap-xp\bin\Release\fwcap-xp.exe
fwcap-xp\obj\Release\
```

The target is compiled for baseline IA-32 instructions for Pentium III-class
XP SP2 hardware, uses no linked C/C++ runtime, and has PE/subsystem version
5.01. Native capture has been exercised on the target Gateway XP SP2 system;
whole-tape and additional-hardware regression testing remains useful.
