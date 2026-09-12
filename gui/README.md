# Shared GUI

The native Win32 GUI is shared by the Windows 11 and Windows XP capture
programs. It is compiled directly into both user-facing executables:

```text
bin\Release\fwcap.exe
fwcap-xp\bin\Release\fwcap-xp.exe
```

Running either executable without arguments opens the GUI. Supplying a capture
filename or any command-line option keeps that same executable headless and
scriptable. There is no separate GUI program to build, install, or distribute.

The GUI currently provides:

- FireWire camcorder discovery;
- DV or HDV source detection from the source's DirectShow output pin;
- live transport state and timecode when the source provides valid timecode;
- Rewind, Stop, Play, Capture, and Fast-forward controls;
- case-insensitive `R`, `S`, `P`, `C`, and `F` shortcuts;
- output-file selection and `.dv`/`.m2t` extension normalization;
- an existing-output conflict check and confirmation before overwrite mode;
- capture progress, duration, byte count, and completion status;
- optional DV video and audio preview during playback on supported systems.

Capture is performed by a hidden headless child instance of the same
executable. The GUI sends a clean stop request through a pipe and reads the
child's progress output. The child owns the native DirectShow capture graph and
writes to `<final-name>.partial`; a clean stop renames that working file to the
final `.dv` or `.m2t` filename.

Playback preview and capture use separate DirectShow graphs. DV preview is
stopped when Capture begins. Preview during either DV or HDV capture is
unsupported and must remain off; if the checkbox remains enabled, do not select
it until capture has stopped. The user should monitor capture on the camcorder
LCD or viewfinder. HDV preview is not supported, although native HDV capture is
fully available.

When both an old final file and an old partial file exist, the current GUI
conflict prompt mentions the partial file. Accepting that prompt enables
overwrite mode for the capture as a whole, so a clean completion can also
replace the old final file.

The shared GUI deliberately uses Win32 controls and APIs that can be built for
the XP SP2 target. Windows 11 and XP retain separate capture implementations
and toolchain settings while sharing this interface source and its resources.
