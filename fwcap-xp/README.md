# fwcap-xp

This is an isolated XP SP2 x86 capture target. It is deliberately not part of
`firewire-capture.sln` and does not modify the modern `fwcap.exe` project,
source, libraries, or build outputs.

The initial target compiles the XP-validated native capture implementation from
`tools/xp-sp2-probe/main.cpp` into a separately named executable. Its current
capture behavior is the validated ten-second native DV/HDV test:

```cmd
fwcap-xp.exe capture.dv
fwcap-xp.exe capture.m2t
```

The source selects `DV A/V Out` or `MPEG2TS Out` from the connected camcorder,
writes native samples, and reports sample and byte counts. The production CLI
features such as indefinite capture, Enter-to-stop, progress, timecode, and
full option parsing will be added in this isolated target after the executable
is validated as a separate artifact.

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
