# firewire-capture
`firewire-capture` is an experimental Windows utility for archival capture of legacy video tape over IEEE-1394 (FireWire).
It supports the capture of both DV and HDV format tapes.
It outputs the capture as unmodified, not transcoded .dv or .m2t files

## Table of contents

- [Licensing and third-party components](#licensing-and-third-party-components)
- [Usage](#usage)
- [FireWire options for your PC](#firewire-options-for-your-pc)
  - [1. Native FireWire port on the PC](#1-native-firewire-port-on-the-pc)
  - [2. Thunderbolt port plus Apple Thunderbolt/FireWire adapters](#2-thunderbolt-port-plus-apple-thunderboltfirewire-adapters)
  - [3. PCIe FireWire card in a desktop PC](#3-pcie-firewire-card-in-a-desktop-pc)
  - [4. External Thunderbolt-to-PCIe enclosure plus PCIe FireWire card](#4-external-thunderbolt-to-pcie-enclosure-plus-pcie-firewire-card)
  - [PCI is not the same as PCIe](#pci-is-not-the-same-as-pcie)
  - [What does NOT work](#what-does-not-work)
- [Camcorder FireWire and tape setup](#camcorder-firewire-and-tape-setup)
- [Bitdefender Webcam Protection](#bitdefender-webcam-protection)
- [How it works](#how-it-works)
- [Project principles](#project-principles)
- [Current status](#current-status)
- [Development environment](#development-environment)
- [Build](#build)
  - [Antivirus issues](#antivirus-issues)
- [Verified hardware results](#verified-hardware-results)
  - [Verified DV connectivity](#verified-dv-connectivity)
  - [Verified HDV connectivity](#verified-hdv-connectivity)
- [Development rules](#development-rules)
  - [Repository hygiene](#repository-hygiene)
- [Possible future development](#possible-future-development)
  - [Expanded AV/C transport control](#expanded-avc-transport-control)
  - [Usability](#usability)
- [Out of scope](#out-of-scope)
- [Contact and contributing](#contact-and-contributing)
- [How to report Security Vulnerabilities](#how-to-report-Security-Vulnerabilities)

## Licensing and third-party components
This project is provided under the license in the accompanying LICENSE file (MIT open source license).
This project is built using Microsoft Windows APIs and does not use or require third-party runtime binaries.

## Usage
`fwcap.exe` is a command-line utility that initiates a capture to a specified file, and starts and stops the camera if the user doesn't do so.

The intended command line is:

```cmd
fwcap [-v] <capturefilename>[.dv|.m2t]
```

Examples of use:

```cmd
fwcap test1
fwcap test.dv
fwcap test.m2t
fwcap -v test
```

The program detects whether the source is DV or HDV and uses `.dv` or `.m2t` accordingly. If the supplied filename has no extension, the appropriate extension is added. If it has the wrong capture extension, the appropriate format extension is appended rather than overwriting the user's filename.

Add `-v` before the output path to show verbose diagnostics.  
Any diagnostic ERRORS that occur are always reported.
During capture, a single updating progress line shows the detected format, duration, bytes, and `?:??:??:??` until reliable timecode extraction is available. A final summary is always shown.

Before running the program:

1. With the camcorder turned off, attach a FireWire connection from the camera to the PC.
2. Put the DV or HDV tape into the camcorder
3. Turn the camcorder on
4. Put the camcorder into VCR/Play mode.  
5. The tape may be stopped, or already playing.
6. Run the command line program.  It automatically starts the tape playing if necessary, and stops tape playback at the end of the capture.
The capture stops when one of the following happens:

- The user presses Enter on the command line to terminate the capture
- 10 seconds after the user stops the tape
- The video ends on the tape and no additional video has been detected for 10 seconds
- The tape comes to its end.


## FireWire options for your PC
`firewire-capture` requires a real IEEE-1394 (FireWire) connection to the camcorder.
A simple USB-to-FireWire cable or passive adapter will not work. USB and
FireWire are different buses; a cable cannot turn an ordinary USB port into
an IEEE-1394 host controller.

There are several practical ways to connect a DV or HDV camcorder to a PC.

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
        -> Thunderbolt 2 cable
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

For example, the StarTech PEX1394B3 is an OHCI-compliant PCIe x1 FireWire
controller using a Texas Instruments XIO2213B chipset. It provides:

- one 6-pin FireWire 400 (IEEE-1394a) port;
- two 9-pin FireWire 800 (IEEE-1394b) ports.

StarTech currently lists the card as compatible with Windows 11.

A PCIe x1 card can normally be installed in a compatible x1 or larger PCIe
slot. This is generally the simplest way to add FireWire to a desktop PC
that has a spare PCIe slot.

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

One example of such an enclosure is the Sonnet Echo Express SE I. It contains
a real PCIe expansion slot and connects that slot to a computer over
Thunderbolt.

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
        -> hdvcap

`firewire-capture` then determines from the connected device and its native media
stream whether it is receiving DV or HDV; the user does not need to specify
the camcorder manufacturer or model.


## Camcorder FireWire and tape setup
Some camcorders support playback of both DV and HDV tapes. For such camcorders, do the following when changing tape formats:
- Put the new tape into the camcorder
- Set the Menu Output playback type to Auto or to the correct format
- Play a bit of tape (and rewind if desired)
- Turn off the camcorder
- Attach the Firewire cable if it isn't already attached.
- Turn on the camcorder.

It is considered safest practice to only connect or disconnect the FireWire cable when the camcorder is powered off. 
Avoid hot-plugging the cable at the camcorder end.

## Bitdefender Webcam Protection
Bitdefender may classify a FireWire DV or HDV camcorder as a camera because Windows exposes it through DirectShow. On first use, Bitdefender Webcam Protection may display a prompt for `fwcap.exe` even though the program is not using the laptop's built-in webcam.

Select **Allow** for `fwcap.exe`, then run the capture command again. If access was previously blocked, check the application permission in:

```text
Bitdefender
  -> Privacy
     -> Video & Audio Protection
        -> Settings
           -> Webcam Protection
```

The `fwcap.exe` entry should have the allowed blue camera icon. Do not disable Webcam Protection globally or add a general antivirus exclusion. If access remains blocked, the program may report `E_ACCESSDENIED` during transport control or receive no samples.

## How it works
The current C++ program:

1. initializes COM;
2. enumerates DirectShow video-input monikers and reads device metadata;
3. filters candidates using FireWire/AVC/61883 device-path information before source activation;
4. binds a candidate source and classifies its output from actual pin/media information;
5. selects the HDV path from the `MPEG2TS Out` output or the DV path from interleaved DV media;
6. uses its HDV sink for HDV and its DV sink for DV;
7. connects the selected source directly to its sink;
8. runs the graph and attempts camera transport PLAY unless the tape is already Playing;
9. shows diagnostics output if a -v parameter is included
10. shows progress as the video is captured
11. stops the graph and attempts camera transport STOP if the video recording ends, or if the user presses ENTER;
12. reports results

The project currently targets **32-bit x86/Win32**.


## Project principles
- **Preserve the tape stream.** HDV and DV should be captured as their transport stream encoding, rather than transcoded during ingest.
- **Keep capture simple.** Editing, transcoding, restoration, and media management belong elsewhere, such as in FFmpeg or a video editor.
- **Use only ordinary Windows interfaces where possible.**
- **Detect failures.** Long-term archival use should eventually report transport/continuity errors and other evidence of an imperfect capture.
- **Keep milestones small and experimentally verifiable against real hardware.**

## Current status
The  `fwcap.exe` program uses its own native HDV and DV capture code.

`fwcap.exe` has been confirmed to work with the following devices' FireWire capture and output formats:

Canon VIXIA HV30: HDV > `.m2t`, DV > `.dv`
Panasonic PV-GS70D: DV > `.dv`

HDV test captures have produced valid HDV MPEG-2 transport stream:

- 1440 × 1080
- 29.97 fps
- top-field-first
- MPEG-2 video at approximately 25 Mb/s
- 48 kHz stereo MPEG Layer II audio

`ffprobe` will show that the video and audio streams are good, and that two other streams that HDV outputs are not understood.   
This is normal.
The captured `.m2t` and `.dv` files have played correctly with picture and sound in VLC.

## Development environment
Primary development machine: Windows 11.

Installed development components:

- Visual Studio Community 2026 toolchain
- Desktop development with C++
- MSVC Build Tools for x64/x86 (Latest)
- Windows 11 SDK 10.0.28000.2114

VS Code/Kilo may be used as the normal development environment. The Visual Studio IDE is not required for routine builds.

The project uses the Visual Studio 2026 `v145` platform toolset and Windows SDK `10.0.28000.0` (installed SDK package/build 10.0.28000.2114), and remains buildable from the command line with MSBuild.

## Build
Build the x86 Release configuration from a developer environment in which MSBuild is available:

```cmd
msbuild firewire-capture.sln /m /p:Configuration=Release /p:Platform=x86
```

Expected executable:

```text
bin\Release\fwcap.exe
```

The current x86 target:

- is retained for compatibility with the original development environment
- may make it easier to create support for older Windows versions
- it is not a native-sink implementation requirement.

### Antivirus issues
This program is currently unknown to (unregistered for) antimalware programs.
It might trigger a quarantine at build time for of build products that trigger its hueristics, or for the app at runtime.

During new software development, antivirus software may flag a newly built, unsigned executable. You can use the antivirus' normal per-application and/or per-folder permission mechanisms rather than disabling protection globally.

## Verified hardware results
On a Windows 11 development machine, the x86 Release build successfully:

- found and bound a `Panasonic PV-GS70D`;
- found the stream and used DV handling;
- wrote the requested file through the native DV sink;
- ran and stopped the graph cleanly;
- wrote a correct `.dv` while the tape was playing.
- stopped the tape at capture end, if it was not already stopped;

- found and bound a `Canon VIXIA HV30`;
- found `MPEG2TS Out` and used HDV handling;
- connected those pins directly with `ConnectDirect`;
- wrote the requested file through the native DV sink;
- ran and stopped the graph cleanly;
- wrote a correct `.m2t` while the tape was playing.
- stopped the tape at capture end, if it was not already stopped;

`ffprobe` identified:

DV results as:

```text
Bitrate: 28771 kb/s
Stream #0:0: Video: dvvideo, yuv411p, 720x480 [SAR 8:9 DAR 4:3], 28771 kb/s, 60k fps, 29.97 tbr, 60k tbn
Stream #0:1: Audio: pcm_s16le, 48000 Hz, stereo, s16, 1536 kb/s
```

HDV results as an MPEG-2 transport stream containing 1440x1080, 16:9, 30000/1001 fps, top-field-first BT.709 MPEG-2 Main Profile video at 25 Mb/s and 48 kHz stereo MP2 audio at 384 kb/s:

```text
Bitrate: 25Mb/s,  27380 kb/s
  Stream #0:0[0x810]: Video: mpeg2video (Main) ([2][0][0][0] / 0x0002), yuv420p(tv, bt709, top first), 1440x1080 [SAR 4:3 DAR 16:9], 25000 kb/s, 29.97 fps, 29.97 tbr, 90k tbn, start 1.001000
    Side data:
      CPB properties: bitrate max/min/avg: 25000000/0/0 buffer size: 7340032 vbv_delay: N/A
  Stream #0:1[0x814]: Audio: mp2 (mp3float) ([3][0][0][0] / 0x0003), 48000 Hz, stereo, fltp, 384 kb/s, start 0.937767
  Stream #0:2[0x815]: Unknown: none ([160][0][0][0] / 0x00A0), start 0.937767
  Stream #0:3[0x811]: Unknown: none ([161][0][0][0] / 0x00A1), start 0.937767
  Unsupported codec with id 0 for input stream 2
  Unsupported codec with id 0 for input stream 3
```

Those two additional streams are a normal part of HDV. FFmpeg does not recognize them, but they are harmless and can be ignored.
  

### Verified DV connectivity
With the Panasonic MiniDV camcorder connected over FireWire, the program discovered the source without using its manufacturer or model as the format decision:

- DirectShow FriendlyName: `Microsoft DV Camera and VCR`;
- native output pin: `DV A/V Out`;
- major type: `MEDIATYPE_Interleaved`;
- DV format: `FORMAT_DvInfo`;
- advertised sample size: 120,000 bytes;
- DV output connected directly to the in-process discard sink;
- graph `Run`, transport PLAY, graph `Stop`, and transport STOP all returned `S_OK` on a successful run;
- the sink accepted 272 native DV samples during approximately ten seconds of playback.

The current DV implementation opens/creates the requested `.dv` path and writes received native DV sample payloads without transcoding. A short capture produced a playable file with matching DV video and PCM audio; FFmpeg reported invalid embedded timecode metadata and two concealed video bitstream errors during strict decoding, which require further source/tape comparison.

### Verified HDV connectivity
The native HDV sink produced a byte-preserving controlled comparison against the previously proven reference path. A separate approximately 42-minute native-sink capture wrote 8,455,295,488 bytes, stopped automatically after ten seconds without media activity, and played correctly in VLC.

Automatic transport control was verified from a stopped tape. The bound HV30 source filter exposes `IAMExtTransport`; its device moniker and `MPEG2TS Out` pin do not. `put_Mode(ED_MODE_PLAY)` and `put_Mode(ED_MODE_STOP)` both returned `S_OK`, and delayed mode reads confirmed both transitions. A resulting 7.3929-second, 25,122,816-byte capture had the same native HDV stream characteristics according to `ffprobe`.


## Development rules
- Work in small, reviewable commits.
- Test against real hardware at each significant milestone.
- Measure before changing code when diagnosing DirectShow behavior.
- Do not claim capture success merely because a graph runs.
- Verify produced media independently.

### Repository hygiene
Generated build products and captures should not be committed. The repository `.gitignore` excludes, among other things:

```text
.vs/
bin/
obj/
*.m2t
*.ax
*.dll
*.exe
*.pdb
*.lib
*.exp
```

## Possible future development

### Expanded AV/C transport control

Add controls beyond the minimal automatic PLAY and STOP established in Milestone 2.5 only after the capture path itself is dependable.

Potential controls:

- Pause
- Rewind
- Fast-forward
- tape/timecode/status reporting

Keep transport control separate from stream writing so either component can be tested independently.

### Usability

Only after HDV and DV capture cores are dependable, consider:

- device listing;
- friendlier CLI commands;
- automatic format detection;
- optional scene splitting;
- capture-session metadata;
- a simple GUI.

A GUI is not a prerequisite for a useful archival tool, but may be a nice to have for future development


## Out of scope
`firewire-capture` is not intended to become:

- a nonlinear editor;
- a transcoder;
- a restoration suite;
- a media player;
- a general-purpose FFmpeg replacement.

Captured material can be processed by dedicated tools after preservation ingest.

## Contact and contributing

For bugs, capture problems, compatibility reports, or concrete feature requests,
please open a GitHub Issue.

For questions, ideas, hardware experiences, or general discussion, use GitHub Discussions.

Pull requests are welcome.

## How to report Security Vulnerabilities
For `security vulnerabilities`, please follow the instructions in `SECURITY.md`
rather than opening a public issue.