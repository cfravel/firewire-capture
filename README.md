# firewire-capture

`firewire-capture` is an experimental Windows command-line utility for archival capture of legacy DV and HDV video over IEEE-1394 (FireWire).

The application preserves the native stream delivered by the camcorder. It does not decode, transcode, or re-encode video or audio during capture.

## Table of Contents

- [License](#license)
- [Project Status](#project-status)
- [Usage](#usage)
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
- [Repository Hygiene](#repository-hygiene)
- [Contact and Contributing](#contact-and-contributing)
- [Security Vulnerabilities](#security-vulnerabilities)

## License

This project is provided under the MIT License. See the accompanying [LICENSE](LICENSE) file for the complete license text.

## Project Status

The current program uses native DirectShow sinks for both supported formats:

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

Native DV capture has been demonstrated:
- with a Canon VIXIA HV30 playing a DV tape and set in Auto play STD
- with a Panasonic PV-GS70D playing a DV tape

Native HDV capture has been demonstrated:
- with a Canon VIXIA HV30 playing an HDV tape and set in Auto play STD

Capture ending has been demonstrated:
- by the video ending on the tape
- by stopping the camcorder transport
- by pressing Enter on the PC to end the command line program


The current Win32/x86 build is the validated configuration. 
Older Windows versions require separate toolchain, driver, and hardware testing.
The native sinks do not inherently require x86, but x64 and older Windows versions have not yet been validated.

## Usage

The command-line form is:

```cmd
fwcap.exe [-v] <capture-name>
```

Examples:

```cmd
fwcap.exe capture
fwcap.exe capture.dv
fwcap.exe capture.m2t
fwcap.exe -v capture
fwcap.exe -v capture.dv
fwcap.exe -v capture.m2t
```

The program discovers the source format from DirectShow. If the requested name does not have the correct extension, the appropriate extension is appended rather than replacing the supplied name. For example, an HDV request named `capture.dv` becomes `capture.dv.m2t`, or a DV request named capture.m2t becomes capture.m2t.dv.

The `-v` option enables successful HRESULT and diagnostic messages. Errors and the final summary are always printed. During capture, the program displays one updating progress line containing:

- detected format;
- current timecode when valid, or `?:??:??:??` when timecode is unavailable or invalid;
- duration;
- bytes received or written.

The final summary reports the normalized output path, Final Valid Timecode, Total Capture Process Duration, Video Duration, Byte Count, and Accepted Sample Count where available.

Total Capture Process Duration includes startup, transport, inactivity timeout, and shutdown timing. 
Video Duration is based on media/timecode information where available.
The two values may differ substantially.

The program can start a stopped tape and stops the transport when capture ends. If the camcorder is already playing, the program leaves it playing and does not issue a second PLAY command. Pressing Enter stops the graph and issues transport STOP.

Capture also stops when DirectShow reports an end condition, the transport reports STOP, or no media activity has been observed for ten seconds. Automatic file segmentation across long gaps is not currently implemented.

## Camera and FireWire Setup

It is safest to only connect or disconnect the FireWire cable while the camcorder is powered off. Avoid hot-plugging the cable at the camcorder end.

Some camcorders support both DV and HDV tapes. When changing tape formats:

1. Insert or switch the tape format.
2. Set the camcorder’s output/playback format to Auto or the required format.
3. Power the camcorder off.
4. Connect or reconnect FireWire while powered off.
5. Power the camcorder on in VCR/playback mode.
6. Play the tape briefly so Windows Device Manager and DirectShow expose the active format.

Power cycling after a format change gives Windows and DirectShow an opportunity to expose the correct source and media format. The application cannot safely infer the new format from tape content if Windows is still exposing the previous format.

On the tested Windows 11 setup:
- Windows Device Manager showed an IEEE-1394 category if a IEEE-1394 (firewire) controller is found.
- If a DV format camcorder in VCR mode was found, Device Manager showed it in the "Imaging Devices" category.
- If an HDV format camcorder in VCR mode was found, Device Manager showed it in the "Sound, video and game controllers" category.

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

An antivirus product may classify a FireWire DV or HDV camcorder as WebCam access because Windows exposes the camcorder through DirectShow. 

For example, on first use, Bitdefender Webcam Protection prompted for `fwcap.exe` even though the laptop's built-in webcam is not being used.
Select **Allow** for `fwcap.exe`. If access was previously blocked, Bitdefender was checked:

```text
Bitdefender
  -> Privacy
     -> Video & Audio Protection
        -> Settings
           -> Webcam Protection
```

The `fwcap.exe` entry should have the allowed blue camera icon. Do not disable Webcam Protection globally or add a general antivirus exclusion. If access remains blocked, the program may report `E_ACCESSDENIED` during transport control or receive no samples.

## How It Works

The program:

1. initializes COM;
2. enumerates DirectShow video-input monikers;
3. reads non-activating device metadata;
4. filters non-FireWire candidates before source activation;
5. binds a supported source filter;
6. classifies DV or HDV from the actual output pin/media type;
7. creates the corresponding native sink;
8. connects the source directly to the sink;
9. queries transport state and issues PLAY only when needed;
10. receives and writes native sample payloads without transcoding;
11. monitors transport, graph events, and media activity;
12. stops and finalizes the capture automatically or when Enter is pressed.

DV timecode is read from native DV data when valid. HDV timecode is read through the source's standard timecode interface when available. Unknown or invalid timecode is displayed as `?:??:??:??`; the program does not manufacture timecode from wall-clock time.

## Development Environment

Primary development environment: Windows 11.

Installed development components:

- Visual Studio Community 2026 toolchain;
- Desktop development with C++;
- MSVC Build Tools for x64/x86;
- Windows 11 SDK 10.0.28000.2114.

VS Code/Kilo may be used as the normal development environment. The Visual Studio IDE is not required for command-line builds.

The project has been successfully built with the Visual Studio 2026 `v145` platform toolset and Windows SDK `10.0.28000.2114`.

## Build

Build the x86 Release configuration from a developer environment in which MSBuild is available:

```cmd
msbuild firewire-capture.sln /m /p:Configuration=Release /p:Platform=x86
```

Expected executable:

```text
bin\Release\fwcap.exe
```

### Antivirus issues

This program is currently unknown to (unregistered for) antimalware programs.
It may quarantine a build product at build time, or the application at runtime, if its heuristics classify the executable as suspicious.

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
- removal of the --hdv-discard testing param and its effect of more HDV diagnostics and not writing data to file
- better DV timecode validation and reporting;
- configurable no-media timeout;
- optional gap detection and segmented output;
- device-removal recovery;
- long-duration and whole-tape regression tests;
- x64 build evaluation;
- evaluation of older Windows compatibility;
- more detailed capture-session metadata;
- optional GUI support.

## Out of Scope

`firewire-capture` is not intended to become:

- a nonlinear editor;
- a transcoder;
- a restoration suite;
- a media player;
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
