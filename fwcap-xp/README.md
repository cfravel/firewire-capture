# fwcap-xp

This is an isolated XP SP2 x86 capture target. It is deliberately not part of
`firewire-capture.sln` and does not modify the modern `fwcap.exe` project,
source, libraries, or build outputs.

The target compiles the XP-validated native capture implementation from
`tools/xp-sp2-probe/main.cpp` into a separately named executable. Its CLI
supports indefinite native DV/HDV capture until Enter or Ctrl+C:

```cmd
fwcap-xp.exe [-v] [--hdv-discard] <capture-name>
```

The source selects `DV A/V Out` or `MPEG2TS Out` from the connected camcorder,
normalizes the output extension to `.dv` or `.m2t`, writes native samples, and
reports elapsed time, sample count, and byte count. `--hdv-discard` accepts and
reports HDV samples without writing them to disk. `-v` is accepted for CLI
compatibility and enables an explicit diagnostic notice.

The XP target intentionally does not share the modern C++ runtime or source
build settings. The implementation remains linked from the validated probe
source while XP-specific capture and CLI behavior is stabilized.

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
5.01. It should still be tested on the Gateway before any longer capture.
