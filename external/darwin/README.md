# SiMPLE Fusion - Darwin/XNU Integration Directory

This directory is reserved for Darwin/XNU reference code that may be
selectively imported into SiMPLE Fusion. Darwin source is NEVER mixed
directly into the core kernel directories (kernel/, boot/, drivers/, fs/).

## Rules

1. Treat Darwin/XNU as a reference architecture first
2. Only import components that can be cleanly isolated
3. Never copy massive subsystems blindly
4. Build compatibility layers around imported code (in kernel/)
5. Document every imported subsystem below
6. Preserve clean abstraction boundaries
7. Never merge incompatible licensing-sensitive code directly

## Directory Layout (planned)

- `xnu/` - XNU kernel reference (cloned from opensource.apple.com)
- `patches/` - Any patches needed to build imported components
- `notes/` - Architecture notes and integration plans

## Imported Subsystems

(none yet - Stage 4 work)

## Architecture Notes

SiMPLE Fusion will draw inspiration from:
- Mach message passing for IPC
- BSD syscall layer concepts
- IOKit-style driver architecture (simplified)
- XNU's layered design: Mach + BSD + IOKit

## License Considerations

Darwin/XNU is released under the Apple Public Source License (APSL).
Any code imported from XNU must retain its original license headers.
SiMPLE-native code remains under its original license.
