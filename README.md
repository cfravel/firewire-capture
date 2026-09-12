# firewire-capture

`firewire-capture` is a Windows application for archival capture of legacy DV
and HDV videotapes over IEEE-1394 (FireWire). It can be used as a graphical
application or as a command-line program.

The application preserves the native stream delivered by the camcorder. It
does not decode, transcode, or re-encode the video or audio during capture.
DV is saved as a native `.dv` stream and HDV is saved as a native MPEG-2
transport stream in an `.m2t` file.

Two builds are provided:

- `fwcap.exe` for Windows 11;
- `fwcap-xp.exe` for 32-bit Windows XP SP2 and later XP systems.

## Table of Contents

- [License](#license)
- [Project Status](#project-status)
- [Usage](#usage)
  - [Install or Place the Executable](#install-or-place-the-executable)
  - [Graphical Application](#graphical-application)
  - [Command-Line Use](#command-line-use)
  - [Important Capture Caveats](#important-capture-caveats)
  - [Recovering a Partial Capture](#recovering-a-partial-capture)
- [Camera and FireWire Setup](#camera-and-firewire-setup)
- [FireWire options for your PC](#firewire-options-for-your-pc)
  - [1. Native FireWire port on the PC](#1-native-firewire-port-on-the-pc)
  - [2. Thunderbolt port plus Apple Thunderbolt/FireWire adapters](#2-thunderbolt-port-plus-apple-thunderboltfirewire-adapters)
  - [3. PCIe FireWire card in a desktop PC](#3-pcie-firewire-card-in-a-desktop-pc)
  - [4. External Thunderbolt-to-PCIe enclosure plus PCIe FireWire card](#4-external-thunderbolt-to-pcie-enclosure-plus-pcie-firewire-card)
  - [PCI is not the same as PCIe](#pci-is-not-the-same-as-pcie)
  - [What does NOT work](#what-does-not-work)
- [AntiVirus Webcam Protection](#antivirus-webcam-protection)
- [How It Works](#how-it-works)
- [Development Environment](#development-environment)
- [Build](#build)
  - [Antivirus Issues](#antivirus-issues)
- [Verified Hardware Results](#verified-hardware-results)
  - [DV Results](#dv-results)
  - [HDV Results](#hdv-results)
- [Project Principles](#project-principles)
- [Future Development](#future-development)
- [Out of Scope](#out-of-scope)
- [Development Rules](#development-rules)
- [Repository Hygiene](#repository-hygiene)
- [Contact and Contributing](#contact-and-contributing)
- [Security Vulnerabilities](#security-vulnerabilities)

## License

This project is provided under the MIT License. See the accompanying [LICENSE](LICENSE) file for the complete license text.

## Project Status

The current program has a shared native Win32 GUI and a headless command-line
mode. Both modes use the same native DirectShow capture engine and support DV
and HDV transport control and capture.

The Windows 11 and Windows XP programs are separate builds so the XP version
can retain its XP SP2 and Pentium III compatibility requirements. They present
the same user-facing workflow while using platform-appropriate build settings.

The capture paths are:

```text
DV camcorder
    -> native interleaved DV samples
    -> native DV sink
    -> .dv archival file
```

```text
HDV camcorder
    -> native MPEG-2 transport samples
    -> native HDV sink
    -> .m2t archival file
```

Format selection is based on the actual DirectShow output pin and media type. The program does not use the camcorder manufacturer, model, or FriendlyName to decide whether a source is DV or HDV.

Current behavior includes:

- automatic FireWire camcorder discovery and DV/HDV format detection;
- Rewind, Stop, Play, Capture, and Fast-forward transport controls;
- optional DV video and audio preview during ordinary playback on supported
  Windows configurations;
- native DV and HDV capture without transcoding;
- capture progress, timecode when the source provides valid timecode, video
  duration, and byte-count reporting;
- safe `.partial` output during capture and automatic final-file rename after
  a clean stop;
- an existing-output conflict check that requires approval before the GUI uses
  overwrite mode, or an explicit `--overwrite` option on the command line.

Native DV capture has been demonstrated:
- with a Canon VIXIA HV30 playing a DV tape and set in Auto play STD
- with a Panasonic PV-GS70D playing a DV tape

Native HDV capture has been demonstrated:
- with a Canon VIXIA HV30 playing an HDV tape and set in Auto play STD

Capture ending has been demonstrated:
- by the video ending on the tape
- by stopping the camcorder transport
- by pressing Enter in command-line mode;
- by pressing Stop in the GUI.

Both current executables are Win32/x86 applications. The XP build targets
Windows XP SP2 and baseline IA-32 instructions for Pentium III-class hardware.
An x64 build is not currently provided.

## Usage

### Install or Place the Executable

There is currently no installer. Copy the executable appropriate for the PC to
a permanent tools directory:

- use `fwcap.exe` on Windows 11;
- use `fwcap-xp.exe` on Windows XP.

You can add that tools directory to the Windows `PATH`. If it is on `PATH`, the
program can be started by name from any Command Prompt. If it is not on `PATH`,
open the tools directory and double-click the executable for the GUI, or run it
from a Command Prompt whose current directory is that tools directory.

The application is portable in the sense that it does not need to be installed
or registered. Keep any captures in a separate folder with enough free disk
space; native DV and HDV capture files are large.

### Graphical Application

Start the appropriate executable with no parameters to open the GUI:

```cmd
fwcap.exe
```

or, on Windows XP:

```cmd
fwcap-xp.exe
```

Double-clicking the executable also starts it with no parameters and opens the
GUI.

The GUI detects the connected FireWire camcorder and reports whether Windows is
currently exposing a DV or HDV source. To capture:

1. Power on the connected camcorder in VCR/playback mode.
2. Confirm that the correct device and DV or HDV format are shown.
3. Use **Browse** to choose the output filename and directory.
4. Cue the tape with Rewind, Play, Stop, and Fast-forward as needed.
5. Select **Capture**. If the tape is stopped, capture starts the transport. If
   it is already playing, the program asks whether to capture from the current
   position.
6. Select **Stop** to finish. A clean stop closes the capture and changes the
   working `.partial` file to the final `.dv` or `.m2t` filename.

The GUI buttons also have case-insensitive keyboard shortcuts:

| Key | Action |
| --- | --- |
| `R` | Rewind |
| `S` | Stop transport or stop the active capture |
| `P` | Play |
| `C` | Capture |
| `F` | Fast-forward |

The application never puts the camcorder into camera recording mode. The
**Capture** command means "save the stream coming from the tape to the PC."

### Command-Line Use

Supplying parameters starts the same executable in headless command-line mode
instead of opening the GUI:

```cmd
fwcap.exe [-v] [--overwrite] [--hdv-discard] <capture-name>
fwcap-xp.exe [-v] [--overwrite] [--hdv-discard] <capture-name>
```

Examples:

```cmd
fwcap.exe capture
fwcap.exe capture.dv
fwcap.exe capture.m2t
fwcap.exe -v capture
fwcap.exe --overwrite capture.dv
fwcap-xp.exe capture
```

The program discovers the source format from DirectShow; the user does not
select DV or HDV on the command line. It normalizes the output extension to
`.dv` for DV or `.m2t` for HDV. A conflicting `.dv` or `.m2t` extension is
replaced. If the name has neither extension, the detected extension is
appended.

Options:

| Option | Purpose |
| --- | --- |
| `-v` | Print successful DirectShow operations and additional diagnostics. |
| `--overwrite` | Permit replacement of an existing final capture and deletion of an existing partial capture. |
| `--hdv-discard` | Diagnostic HDV mode that receives the stream without writing a file. Do not use this option for an ordinary capture or with a DV source. |

Errors and the final summary are printed without `-v`. During capture, the
program displays an updating progress line containing:

- detected format;
- current timecode when valid, or `?:??:??:??` when timecode is unavailable or invalid;
- duration;
- bytes received or written.

The final summary reports the normalized output path, final valid timecode,
total capture-process duration, video duration, byte count, and accepted sample
count where available.

Total Capture Process Duration measures the active capture interval used by the
capture engine. It may include transport and inactivity waiting, but it is not
the elapsed time for the entire program: initial device/graph setup and final
shutdown are outside some or all of that measurement. Video Duration is based
on media/timecode information where available, so the two values may differ
substantially.

The program can start a stopped tape and stops the transport when capture ends.
If the camcorder is already playing, the program does not issue a second Play
command. Press Enter to stop a command-line capture cleanly.

Capture also stops when DirectShow reports an end condition, the transport reports STOP, or no media activity has been observed for ten seconds. Automatic file segmentation across long gaps is not currently implemented.

### Important Capture Caveats

- Connect or disconnect the FireWire cable only while the camcorder is powered
  off. Avoid hot-plugging the small FireWire connector at the camcorder.
- When changing between DV and HDV playback/capture, power off the camcorder,
  unplug and reconnect the FireWire connection, and then power the camcorder
  back on in VCR/playback mode. Windows may otherwise continue exposing the
  previous format even after the tape or camcorder setting changes.
- There is no supported application preview while Capture is running, for
  either DV or HDV. Watch the camcorder's own LCD or viewfinder during capture.
  If the preview checkbox remains available in a current build, do not enable
  it during capture.
- DV preview is only for cueing and ordinary playback when Capture is not
  running. It depends on the DirectShow components installed on the PC.
- HDV preview is not currently supported in the application. Use the camcorder
  display for HDV playback and capture monitoring. This does not affect native
  HDV capture.
- Timecode can be missing or invalid on a tape. The program reports unknown
  timecode rather than inventing a value. Recording date is not currently
  available.
- Make sure the destination has enough free disk space before beginning a long
  capture.

### Recovering a Partial Capture

While capture is active, the program writes to a filename ending in
`.partial`, for example:

```text
family-tape.dv.partial
family-tape.m2t.partial
```

After a normal Stop, the program closes the stream and automatically removes
the final `.partial` suffix, producing `family-tape.dv` or `family-tape.m2t`.

If the program, Windows, or the PC crashes, the `.partial` file is intentionally
left in place. It contains the native data received and written before the
interruption. After the capture program is no longer running, rename the file
by removing only the final `.partial` extension:

```text
family-tape.dv.partial   -> family-tape.dv
family-tape.m2t.partial  -> family-tape.m2t
```

If a file with the desired final name already exists, do not overwrite it just
to remove `.partial`. Either move the old final file to a safe name first or
give the recovered capture a different valid `.dv` or `.m2t` name. This can
happen when a replacement capture was started in overwrite mode but crashed
before the new partial file replaced the old final file.

The recovered file may contain everything captured up to shortly before the
failure and can often be opened by a player or media tool that supports raw DV
or MPEG-2 transport streams. Because the capture did not end cleanly, the last
portion may be incomplete. Preserve the partial file until the recovered media
has been checked.

## Camera and FireWire Setup

Only connect or disconnect the FireWire cable while the camcorder is powered
off. FireWire ports, especially the small unpowered 4-pin camcorder connector,
can be damaged by incorrect or repeated hot-plugging.

Some camcorders support both DV and HDV tapes. When changing tape formats:

1. Stop capture and close any application that is using the camcorder.
2. Insert the tape and set the camcorder's playback/output format to Auto, DV,
   or HDV as appropriate.
3. Power the camcorder off.
4. Unplug and reconnect the FireWire cable while the camcorder is off.
5. Power the camcorder on in VCR/playback mode.
6. Start `fwcap` and confirm that it reports the expected DV or HDV format.
7. If necessary, play the tape briefly so Windows and DirectShow expose the
   active format.

Power cycling after a format change gives Windows and DirectShow an opportunity to expose the correct source and media format. The application cannot safely infer the new format from tape content if Windows is still exposing the previous format.

On the tested Windows 11 setup:

- Device Manager showed an IEEE-1394 category when an IEEE-1394/FireWire
  controller was present.
- A DV camcorder in VCR mode appeared under **Imaging devices**.
- An HDV camcorder in VCR mode appeared under **Sound, video and game
  controllers**.

## FireWire options for your PC

`firewire-capture` requires a real IEEE-1394 (FireWire) connection to the camcorder.
A simple USB-to-FireWire cable or passive adapter will not work. USB and
FireWire are different buses; a cable cannot turn an ordinary USB port into
an IEEE-1394 host controller.

There are several practical ways to connect a DV or HDV camcorder to a PC.

The following configurations have been researched or used successfully; compatibility is system-dependent.

#### 1. Native FireWire port on the PC

The simplest case is a PC that already has an IEEE-1394 (FireWire) port and
a working OHCI FireWire controller.

Connect the camcorder directly to the PC using the appropriate FireWire cable:

    PC FireWire port
        -> FireWire cable
        -> camcorder DV/HDV (IEEE-1394) port

Many DV and HDV camcorders use the small 4-pin FireWire 400 connector.
The PC may have a 4-pin, 6-pin FireWire 400, or 9-pin FireWire 800 connector,
so choose the cable appropriate for the two devices.

FireWire 800 is backward-compatible with FireWire 400 when the appropriate
cable is used. DV and HDV do not require FireWire 800 bandwidth.

#### 2. Thunderbolt port plus Apple Thunderbolt/FireWire adapters

A PC with a compatible Thunderbolt port can use the Apple adapter chain that
has been successfully used with `firewire-capture`.

For a PC with Thunderbolt 3 using a USB-C connector, the complete chain is:

    PC Thunderbolt 3 USB-C port
        -> Apple Thunderbolt 3 (USB-C) to Thunderbolt 2 Adapter
           (model A1790 / EMC 3062)
        -> Apple Thunderbolt to FireWire Adapter
           (model A1463 / EMC 2591)
        -> FireWire cable
        -> camcorder

The Apple A1463 adapter provides a FireWire 800 (9-pin) socket. Therefore,
for the common 4-pin FireWire socket on a DV/HDV camcorder, use a
9-pin-to-4-pin FireWire cable.

IMPORTANT: A USB-C-shaped port is not necessarily a Thunderbolt port.
This adapter chain requires Thunderbolt; it will not work merely because
the computer has a USB-C connector.

The first Apple adapter converts between Thunderbolt 3 and the older
Thunderbolt/Thunderbolt 2 connector. The second converts Thunderbolt to
IEEE-1394b (FireWire 800). The final FireWire cable adapts the physical
FireWire connector to the camcorder's 4-pin FireWire port.

This arrangement provides a real IEEE-1394 connection; it is not a
USB-to-FireWire conversion.

#### 3. PCIe FireWire card in a desktop PC

A desktop PC without built-in FireWire can use an IEEE-1394 PCI Express
(PCIe) controller card in an available PCIe expansion slot:

    camcorder
        -> FireWire cable
        -> PCIe FireWire controller card
        -> motherboard PCIe slot

For example, research suggests that the StarTech PEX1394B3 is an OHCI-compliant PCIe x1 FireWire
controller using a Texas Instruments XIO2213B chipset. It provides:

- one 6-pin FireWire 400 (IEEE-1394a) port;
- two 9-pin FireWire 800 (IEEE-1394b) ports.

StarTech currently lists the card as compatible with Windows 11.

A PCIe x1 card can normally be installed in a compatible x1 or larger PCIe
slot. This is generally the simplest way to add FireWire to a desktop PC
that has a spare PCIe slot.

This is not to imply that the StarTech has been tested by this developer or to imply universal compatibility solely from the product examples. The important requirements are:

- real IEEE-1394/FireWire;
- OHCI host controller;
- Windows-compatible driver stack;
- correct cable and powered-off connection procedure.

#### 4. External Thunderbolt-to-PCIe enclosure plus PCIe FireWire card

A laptop, mini-PC, or other computer that has Thunderbolt but no internal
PCIe expansion slot can potentially use an external Thunderbolt-to-PCIe
expansion enclosure.

For example:

    camcorder
        -> FireWire cable
        -> PCIe FireWire controller card
        -> external PCIe expansion enclosure
        -> Thunderbolt cable
        -> PC Thunderbolt port

One example of such an enclosure appears to be the Sonnet Echo Express SE I. It contains
a real PCIe expansion slot and connects that slot to a computer over
Thunderbolt.

This is not to imply that the Sonnet has been tested by this developer 
or to imply universal compatibility solely from the product examples.

A suitable OHCI PCIe FireWire card can then be installed in the enclosure,
subject to compatibility between the particular PCIe card, enclosure,
computer, and Windows.

This is fundamentally different from a USB-to-FireWire adapter: Thunderbolt
can carry PCI Express traffic, allowing the external enclosure to expose a
real PCIe device to the computer.

On current Windows systems, some USB4-equipped computers can also support
compatible Thunderbolt PCIe expansion hardware, but ordinary USB does not.
Check the requirements of the particular expansion enclosure and computer.

#### PCI is not the same as PCIe

Older computers may have legacy PCI FireWire cards. PCI and PCI Express
(PCIe) are different electrical interfaces and the cards are not
interchangeable.

An old PCI FireWire card cannot simply be installed in a modern PCIe
expansion enclosure. For a new installation, a PCIe FireWire controller is
generally the more practical choice.

#### What does NOT work

Do not use a passive cable advertised as:

    USB -> FireWire

An ordinary USB port cannot communicate directly with an IEEE-1394 DV/HDV
camcorder merely by changing the connector.

Similarly, a USB-C connector by itself does not imply Thunderbolt support.
Verify that the port actually supports the Thunderbolt (or specifically
supported USB4) functionality required by the adapter or expansion system.

The objective in all of the working configurations above is the same:

    DV/HDV camcorder
        -> IEEE-1394 (FireWire)
        -> real IEEE-1394 OHCI host controller
        -> Windows
        -> fwcap

`firewire-capture` then determines from the connected device and its native media
stream whether it is receiving DV or HDV; the user does not need to specify
the camcorder manufacturer or model.


## Antivirus Webcam Protection

An antivirus product may classify access to a FireWire DV or HDV camcorder as
webcam access because Windows exposes the camcorder through DirectShow.

For example, on first use, Bitdefender Webcam Protection prompted for
`fwcap.exe` even though the laptop's built-in webcam was not being used. Select
**Allow** for the appropriate `fwcap.exe` or `fwcap-xp.exe`. If access was
previously blocked, check the antivirus application's per-program webcam
permissions. In the tested Bitdefender version, the path was:

```text
Bitdefender
  -> Privacy
     -> Video & Audio Protection
        -> Settings
           -> Webcam Protection
```

The executable's entry should be allowed. Do not disable Webcam Protection
globally or add a broad antivirus exclusion. If access remains blocked, the
program may report `E_ACCESSDENIED` during device or transport access, or it may
receive no samples.

## How It Works

With no parameters, the executable initializes the shared Win32 GUI, discovers
the FireWire source, and exposes device status, transport controls, output-file
selection, and optional DV playback preview. Selecting Capture starts a hidden
headless instance of the same executable and communicates with it through
pipes. The capture process owns the native DirectShow capture graph. This keeps
the GUI responsive while retaining one capture implementation for graphical
and command-line use.

With command-line parameters, the executable runs the capture engine directly:

1. initializes COM;
2. enumerates DirectShow video-input monikers;
3. reads non-activating device metadata;
4. filters non-FireWire candidates before source activation;
5. binds a supported source filter;
6. classifies DV or HDV from the actual output pin/media type;
7. normalizes the output filename and creates its `.partial` working file;
8. creates the corresponding native sink;
9. connects the source directly to the sink;
10. queries transport state and issues Play only when needed;
11. receives and writes native sample payloads without transcoding;
12. periodically flushes written data and reports progress;
13. monitors transport, graph events, and media activity;
14. stops automatically or in response to the GUI/keyboard;
15. closes the graph and renames the working file to its final `.dv` or `.m2t`
    name after a clean capture.

The playback-preview graph is separate from the capture graph. DV preview is
stopped before capture because the camcorder and its DirectShow output cannot
be shared reliably between those graphs. HDV preview is not implemented because
a suitable built-in MPEG-2 DirectShow decoding path was not available on the
tested system.

DV timecode is read from native DV data when valid. HDV timecode is read through the source's standard timecode interface when available. Unknown or invalid timecode is displayed as `?:??:??:??`; the program does not manufacture timecode from wall-clock time.

## Development Environment

Primary development environment: Windows 11. The XP-compatible product is
cross-built on Windows 11 and tested separately on Windows XP SP2 hardware.

Installed development components:

- Visual Studio Community 2026 toolchain;
- Desktop development with C++;
- MSVC Build Tools for x64/x86;
- Windows 11 SDK 10.0.28000.2114.

VS Code/Kilo may be used as the normal development environment. The Visual Studio IDE is not required for command-line builds.

The projects have been successfully built with the Visual Studio 2026 `v145`
platform toolset and Windows SDK `10.0.28000.2114`.

## Build

Build the Windows 11 Win32 Release configuration from a developer environment
in which MSBuild is available:

```cmd
msbuild firewire-capture.sln /m /p:Configuration=Release /p:Platform=x86
```

Expected executable:

```text
bin\Release\fwcap.exe
```

The XP project is deliberately separate from the modern solution. Build it
from the `fwcap-xp` directory:

```cmd
msbuild fwcap-xp.vcxproj /m /p:Configuration=Release /p:Platform=Win32
```

Expected executable:

```text
fwcap-xp\bin\Release\fwcap-xp.exe
```

Both projects compile the shared `gui\main.cpp` and icon resources directly
into their respective executable. There is no separate GUI executable to
distribute.

### Antivirus issues

These executables are currently unsigned and may be unknown to antimalware
reputation systems. An antivirus application may quarantine a build product at
build time or block it at runtime if its heuristics classify the executable as
suspicious.

During new software development, antivirus software may flag a newly built, unsigned executable. You can use the antivirus' normal per-application and/or per-folder permission mechanisms rather than disabling protection globally.

## Verified Hardware Results

### DV Results

The Panasonic MiniDV source was exposed by DirectShow as:

```text
Microsoft DV Camera and VCR
```

Its native output was:

```text
DV A/V Out
MEDIATYPE_Interleaved
FORMAT_DvInfo
120,000-byte samples
```

Real captures demonstrated:

- automatic PLAY and STOP;
- direct connection to the native DV sink;
- native `.dv` file creation and writing;
- 720x480 NTSC DV video;
- 29.97 fps;
- 48 kHz stereo PCM audio;
- successful VLC playback.

On Windows 11, the GUI's optional DV playback preview has also been demonstrated
with the correct 4:3 display aspect ratio and playback audio. Preview is not
used while capture is active.

FFmpeg has reported invalid embedded DV timecode on some source captures. The program preserves the received DV bytes and does not rewrite that metadata.

The Canon VIXIA HV30 also successfully played a Panasonic-recorded DV tape through the native DV path.

### HDV Results

The Canon VIXIA HV30 exposes:

```text
MPEG2TS Out
```

The native HDV sink has been verified with:

- 1440x1080 MPEG-2 Main video;
- 16:9 display aspect ratio;
- 29.97 fps;
- top-field-first;
- BT.709;
- approximately 25 Mb/s video;
- 48 kHz stereo MP2 audio;
- 384 kb/s audio;
- VLC playback;
- automatic PLAY/STOP;
- automatic stop after ten seconds without media activity;
- approximately 42-minute native capture producing 8,455,295,488 bytes.

Native HDV capture works without an in-application preview. The camcorder LCD
or viewfinder is used for monitoring.

FFmpeg reports auxiliary MPEG-TS streams `0xA0` and `0xA1` as unknown. These are expected from the source and do not prevent the primary video and audio streams from decoding.

## Project Principles

- **Preserve the tape stream.** DV and HDV are captured in their native sample formats without transcoding during ingest.
- **Keep capture simple.** Editing, transcoding, restoration, and media management belong elsewhere.
- **Detect failures.** Long-term archival use should report transport, continuity, and other evidence of imperfect capture.
- **Use ordinary Windows interfaces where possible.**
- **Keep milestones small and experimentally verifiable against real hardware.**

## Future Development

Possible future work includes:

- More camcorders tested;
- More testing of end-of-tape encountered (video all the way to end);
- MPEG-TS continuity-counter diagnostics;
- improved HDV timecode reporting;
- removal or replacement of the `--hdv-discard` diagnostic option;
- better DV timecode validation and reporting;
- configurable no-media timeout;
- optional gap detection and segmented output;
- device-removal recovery;
- long-duration and whole-tape regression tests;
- x64 build evaluation;
- broader Windows 11 and Windows XP hardware testing;
- more detailed capture-session metadata;
- possible HDV playback-preview support through a suitable decoder;
- evaluation of a single-graph preview-and-capture architecture.

## Out of Scope

`firewire-capture` is not intended to become:

- a nonlinear editor;
- a transcoder;
- a restoration suite;
- a general-purpose media player;
- a general-purpose FFmpeg replacement.

Captured material can be processed by dedicated tools after preservation ingest.

## Development Rules

- Work in small, reviewable commits.
- Test against real hardware at each significant milestone.
- Measure before changing code when diagnosing DirectShow behavior.
- Do not claim capture success merely because a graph runs.
- Verify produced media independently.
- Do not commit generated captures or build products.
- Do not commit proprietary components.

## Repository Hygiene

Generated build products, captures, and object files must not be committed. The repository `.gitignore` excludes, among other things:

```text
.vs/
bin/
obj/
*.dv
*.m2t
*.dll
*.exe
*.pdb
*.lib
*.exp
*.obj
```

## Contact and Contributing

For bugs, capture problems, compatibility reports, or concrete feature requests, open a GitHub Issue.

For questions, ideas, hardware experiences, or general discussion, use GitHub Discussions.

Pull requests are welcome.

## Security Vulnerabilities

For security vulnerabilities, follow the instructions in `SECURITY.md` rather than opening a public issue.
