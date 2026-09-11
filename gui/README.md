# Shared GUI

Phase 1 provides a shared native Win32 shell compiled directly into the two
user-facing executables:

```text
bin\Release\fwcap.exe
fwcap-xp\bin\Release\fwcap-xp.exe
```

The shell intentionally does not connect to the capture engine yet. It is a
checkpoint for the shared layout, Win32 control set, XP-compatible toolchain,
output dialogs, transport button wiring, and keyboard affordances. Capture,
transport, status events, and preview will be connected in later phases.

Running either executable without arguments opens this shell. Supplying a
filename or CLI option keeps the executable headless and scriptable.
