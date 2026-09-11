# Shared GUI

Phase 1 provides a shared native Win32 shell compiled into separate modern and
XP executables:

```text
gui-modern\bin\Release\fwcap-gui.exe
gui-xp\bin\Release\fwcap-xp-gui.exe
```

The shell intentionally does not connect to the capture engine yet. It is a
checkpoint for the shared layout, Win32 control set, XP-compatible toolchain,
output dialogs, transport button wiring, and keyboard affordances. Capture,
transport, status events, and preview will be connected in later phases.
