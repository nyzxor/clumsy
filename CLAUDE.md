# CLAUDE.md - AI Assistant Guide for Clumsy

## Project Overview

**Clumsy** is a Windows network condition simulator that intercepts network packets at the system level to simulate poor network conditions (lag, packet loss, throttling, etc.) without requiring proxy setup or code modification.

- **Language:** C
- **Build System:** Zig (primary), GENie/Premake (legacy)
- **Platform:** Windows 7/8/10/11 only
- **Architectures:** x86, x64
- **Version:** 0.3
- **License:** MIT

## Quick Reference

### Build Commands

```bash
# Default build (x64, Debug, WinDivert sign variant A)
zig build

# Release build
zig build -Darch=x64 -Dconf=Release -Dsign=A

# Ship build (production, windowed)
zig build -Darch=x64 -Dconf=Ship -Dsign=A

# x86 build
zig build -Darch=x86 -Dconf=Release -Dsign=A

# With custom Windows SDK path
zig build -Dwindows_kit_bin_root="C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0"
```

### Build Output

Output goes to `zig-out/<arch>_<conf>_<sign>/` (e.g., `zig-out/x64_Release_A/`)

Required runtime files:
- `clumsy.exe`
- `WinDivert.dll`
- `WinDivert64.sys` (or `WinDivert32.sys` for x86)
- `config.txt`

## Codebase Structure

```
src/                    # Core source code
├── common.h            # Central header - types, constants, module interface
├── main.c              # Entry point, GUI orchestration, config loading
├── divert.c            # WinDivert wrapper, packet capture/injection threads
├── packet.c            # Packet linked-list implementation
├── elevate.c           # Admin privilege elevation
├── utils.c             # Shared utilities, UI sync callbacks
└── [modules]           # Simulation modules (see below)

external/               # Pre-compiled dependencies
├── WinDivert-2.2.0-A/B/C/  # Packet interception library (3 signing variants)
└── iup-3.30_*/         # GUI library (4 variants for Win32/64, MSVC/MinGW)

etc/                    # Resources and config
├── clumsy.rc           # Windows resources (icon, manifest)
├── config.txt          # Filter presets
├── clumsy*.manifest    # Application manifests
└── clumsy-icon.ico     # Application icon

scripts/                # Test utilities
└── send_udp_nums.py    # UDP packet generator for testing
```

## Simulation Modules

Processing order: lag → drop → throttle → duplicate → ood → tamper → reset → bandwidth

| Module | File | Purpose |
|--------|------|---------|
| Lag | `src/lag.c` | Delay packets (0-15,000ms) |
| Drop | `src/drop.c` | Random packet dropping (0-100%) |
| Throttle | `src/throttle.c` | Queue packets, release in bursts |
| Duplicate | `src/duplicate.c` | Create packet copies (2-50x) |
| Out-of-Order | `src/ood.c` | Reorder packets randomly |
| Tamper | `src/tamper.c` | Corrupt packet bytes |
| Reset | `src/reset.c` | Inject TCP RST packets |
| Bandwidth | `src/bandwidth.c` | Cap throughput (token bucket) |

## Architecture & Key Patterns

### Module Interface

Each module implements this interface (defined in `src/common.h`):

```c
typedef struct {
    short processTriggered;  // Module enabled flag
    PSHORT* setup_ui();      // Create UI controls
    void startUp();          // Initialize when enabled
    void closeDown();        // Cleanup when disabled
    void process(PacketNode *head, PacketNode *tail);  // Process packets
} Module;
```

### Threading Model

- **Main thread:** GUI event handling (IUP)
- **Read thread:** `divertReadLoop()` - captures packets from WinDivert (3ms cycles)
- **Clock thread:** `divertClockLoop()` - triggers module processing (40ms cycles)
- **Synchronization:** Mutex protects packet queue access

### Packet Processing Pipeline

1. WinDivert captures packets at kernel level
2. Packets queued as doubly-linked list (max 2048)
3. Clock thread triggers module chain processing
4. Modules modify/drop/create packets in order
5. Remaining packets re-injected via WinDivert

## Development Conventions

### Code Style

- C89/C90 compatible code
- Windows API naming conventions (BOOL, DWORD, etc.)
- Module functions prefixed with module name (e.g., `lagSetupUI`, `lagProcess`)
- Use `LOG()` macro for debug output (only in `_DEBUG` builds)

### Adding New Modules

1. Create `src/newmodule.c` following existing module patterns
2. Add module declaration to `src/common.h`
3. Register in module array in `src/main.c`
4. Update `build.zig` to include new source file

### UI Development

- Uses IUP library for portable GUI
- Controls created in module's `setupUIFunc()`
- Use `uiSync*` utilities from `utils.c` for value synchronization
- Attributes set via `IupSetAttribute()` / `IupGetAttribute()`

### Build System Notes

- **Zig build:** Primary build system, cross-compiles C code
- **Resource compilation:** Requires Windows SDK `rc.exe`
- **Debug vs Release:** Debug uses console subsystem; Release/Ship use Windows subsystem
- **WinDivert variants (A/B/C):** Different code-signing certificates for driver loading

## Dependencies

### WinDivert 2.2.0

- Kernel-level packet interception
- Three signing variants available (A, B, C)
- Requires admin privileges to run
- Headers: `external/WinDivert-2.2.0-*/include/windivert.h`

### IUP 3.30

- Cross-platform GUI toolkit
- Static libraries in `external/iup-3.30_*/`
- Supports both MinGW and MSVC toolchains

### System Libraries

`comctl32`, `winmm`, `ws2_32`, `kernel32`, `gdi32`, `comdlg32`, `uuid`, `ole32`

## Testing

### Manual Testing

1. Build with Debug configuration
2. Run `clumsy.exe` as administrator
3. Select filter from dropdown or enter custom WinDivert filter
4. Enable modules and configure parameters
5. Click Start to begin packet interception

### Test Scripts

```bash
# Generate UDP test traffic
python scripts/send_udp_nums.py

# Use batch files for specific scenarios
scripts/start_nums_cs.bat
```

### Filter Presets

`etc/config.txt` contains 12 pre-configured filters:
- Localhost (all/tcp/udp)
- Inbound/outbound traffic
- IP-specific and port-specific filters

## Important Files for AI Assistants

When modifying clumsy, these are the most important files to understand:

- `src/common.h` - All shared types and module interface
- `src/main.c` - GUI setup and module orchestration
- `src/divert.c` - Core packet processing engine
- `build.zig` - Build configuration and dependencies
- `etc/config.txt` - Runtime filter configuration

## Troubleshooting

### Build Issues

- **Missing rc.exe:** Set `-Dwindows_kit_bin_root` to Windows SDK bin path
- **x86 build fails:** Known Zig bug workaround in `etc/chkstk.s`
- **Library not found:** Check `external/` directory has correct IUP/WinDivert variants

### Runtime Issues

- **Requires admin:** WinDivert needs elevated privileges
- **Driver loading fails:** Try different WinDivert signing variant (A/B/C)
- **No packets captured:** Check WinDivert filter syntax

## Recent Changes

- Migrated to Zig build system from GENie
- Added support for WinDivert signing variants A/B/C
- Fixed x86 build issues
- Output directory reorganized to `zig-out/`
