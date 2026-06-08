# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A two-part desk companion for Claude Code running on a **Seeed Wio Terminal**
(SAMD51, 320×240 LCD): an Arduino **firmware** that renders an animated ASCII pet
plus session/usage panels, and a Python **host bridge** that feeds it data from
`~/.claude` and Claude's usage API over BLE. It's a Wio port of
[`claude-desktop-buddy`](https://github.com/Links17/claude-desktop-buddy).

`README.md` is the source of truth for hardware setup, wiring, and the full
troubleshooting log — read it for anything physical-device related.

## Commands

### Firmware (`firmware/claude_buddy/`)

```bash
# Default build (ASCII buddy + BLE + usage). On macOS pin the bundled Wio LCD lib:
LCD=~/Library/Arduino15/packages/Seeeduino/hardware/samd/1.8.5/libraries/Seeed_Arduino_LCD
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal --library "$LCD" claude_buddy.ino

# Build + flash to a board (device must be in bootloader: double-tap power switch):
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal --library "$LCD" \
  -u -p /dev/cu.usbmodemXXXX claude_buddy.ino   # then UNPLUG/REPLUG to run

# Emulator build (canned demo data, no BLE — for the web SAMD emulator):
arduino-cli compile ... --build-property "compiler.cpp.extra_flags=-DMOCK_DATA" claude_buddy.ino

# GIF character packs (opt-in; pulls in Seeed_FS/SFUD, needs a microSD):
arduino-cli compile ... --build-property "compiler.cpp.extra_flags=-DBUDDY_GIF" ... claude_buddy.ino

# Native unit test for the base64 decoder (no board needed):
g++ -std=c++17 firmware/claude_buddy/test/test_b64.cpp -o /tmp/test_b64 && /tmp/test_b64
```

The `--library "$LCD"` override is only needed when a conflicting generic
`TFT_eSPI` is installed; otherwise omit it (CI does). The LCD driver ships inside
the board package.

### Host bridge (`host/`)

```bash
cd host
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt        # only dep is bleak; claude_meter is stdlib-only
python buddy_ble_bridge.py             # finds "Claude Wio" over BLE and streams

# Lint (what CI runs):
python -m py_compile host/buddy_ble_bridge.py host/claude_meter/*.py
```

CI (`.github/workflows/build.yml`) on every push/PR: compiles the default
firmware, runs the native base64 test, and byte-compiles the host. There is no
pytest/ruff config — keep the host import-clean and byte-compilable.

## Architecture

### Single BLE link, one data source

The Wio holds exactly **one** BLE connection. You pick *either* this project's
`buddy_ble_bridge.py` (pet + sessions + real usage gauges, no live permission
prompts) *or* Claude Desktop's built-in Hardware Buddy (pet + live approve/deny
prompts, no usage gauges). The firmware speaks both protocols; the bridge is the
primary path. The bridge derives everything from `~/.claude`, which has no
pending-prompt info — that's why the approval footer only lights up under Desktop.

### Wire protocol (newline-delimited JSON, NUS)

BLE is a Nordic UART Service (`6e400001-…`); RX write char is
`6e400002-…`. The host sends `\n`-terminated JSON, chunked to ≤160 bytes per
write (`send_line` in the bridge); the firmware reassembles by newline in
`data.h::_LineBuf` and dispatches in `_applyJson`. Three message shapes, all on
the same channel:

1. **Heartbeat** `{total, running, waiting, msg, entries, tokens, tokens_today}`
   — drives the pet state machine + SESSIONS pane. Built by `heartbeat()`.
2. **Usage snapshot** `{v:1, session:{pct,reset_s}, week:{pct,reset_s,reset_label}, today:{tokens}, …}`
   — drives the USAGE gauges. Built by `claude_meter.snapshot.build_snapshot`.
   `reset_s` counts down **locally at 1 Hz** on-device between pushes
   (`dataUsageTick`) so it never looks frozen.
3. **Time sync** `{time:[epoch_sec, tz_offset_sec]}` — sets the soft RTC once.

The same JSON path also handles permission prompts (`prompt:{id,tool,hint}`) and
folder-push/GIF commands (`xferCommand`, via `b64.h`/`xfer.h`) when driven by a
source that sends them.

### Host data pipeline (`host/claude_meter/`, vendored, stdlib-only)

`scanner.IncrementalScanner` tails `~/.claude/projects/**/*.jsonl` by byte offset
→ `UsageEvent` stream → `snapshot.build_snapshot` produces the v:1 dict, combining
a **local estimate** (`aggregator` + `pricing` caps) with an optional **exact
server probe**. The bridge fetches exact numbers from the undocumented
`GET https://api.anthropic.com/api/oauth/usage` endpoint (`probe_usage`) every
`USAGE_POLL_S` (60 s) and overrides the estimate; on failure (401/rate-limit) it
falls back to the estimate, which over-counts cache tokens. `claude_meter` is
vendored from the author's *claude-usage* meter — keep it self-contained; don't
add third-party deps.

**OAuth token source matters:** `_oauth()` reads the *current* token from the
macOS **Keychain** (`security find-generic-password -s "Claude Code-credentials"`)
because `~/.claude/.credentials.json` is usually expired. Porting to
Linux/Windows means replacing `_oauth()`.

### Firmware structure (`firmware/claude_buddy/`)

- `claude_buddy.ino` — `setup`/`loop`, the `PersonaState` machine
  (sleep/idle/busy/attention/celebrate/dizzy/heart, derived in `derive()`), the
  landscape split view (left = buddy, right = one of 3 panes cycled by button B),
  and button/accelerometer handling.
- `data.h` — the wire-protocol parser **and** the three runtime modes checked in
  priority order: `demo` (auto-cycling fakes), `live` (JSON seen in last ~10 s),
  `asleep` (zeros).
- `wio_platform.*` — hardware shim: display/sprite, buttons, accelerometer
  (shake/face-down), buzzer, and a **soft RTC** (M5-compatible structs so `data.h`
  ports unchanged).
- `ble_bridge.*` — rpcBLE NUS peripheral. `buddy*.{h,cpp}` + `buddy_sp_*.cpp` —
  the ASCII pet engine; each of the 18 species is one file exposing 7 state
  functions in `PersonaState` order (`Species::states[7]`).
- `character.*` + `xfer.h`/`b64.h` — GIF character packs, opt-in `-DBUDDY_GIF`.
- `stats.h` (mood/fed/energy/level) + `prefs_compat.h` (settings store).

## Non-obvious constraints (these are deliberate; don't "fix" them)

- **Persistence is RAM-only.** Writing stats/species to flash (`FlashStorage`)
  blocks for ms and wedges the rpcBLE link, so stats reset on reboot.
  `prefs_compat.h` is a RAM store, not real NVS.
- **8-bit sprite, not 16-bit.** A full-screen 16-bit sprite (≈150 KB) won't fit
  beside the BLE stack's RAM; the firmware uses an 8-bit sprite (≈77 KB). A red
  "sprite alloc failed" screen means you're out of RAM.
- **No re-advertise after disconnect.** Re-advertising crashed rpcBLE, so after a
  BLE drop you must **replug the Wio** before the bridge can reconnect.
- **GIF/filesystem is opt-in** because `Seeed_FS`/`SD`'s global initializer faults
  on cold boot. The default build omits it (`wio_platform.h` gates `BUDDY_FS`).
- **`MOCK_DATA` builds** stub out all `ble*` functions in `claude_buddy.ino` and
  force demo mode — the emulator has no filesystem and can't run the GIF decoder,
  so it always shows the ASCII buddy and injects a synthetic permission prompt on
  a timer to exercise the approval footer.

## Build/debug gotchas (learned the hard way on real hardware)

- **Flashing needs a MANUAL bootloader entry before EVERY upload.** Ask the user
  to double-tap the power switch (down twice quickly), **wait for them to
  confirm**, then upload — the 1200-baud auto-reset does *not* work when the app
  is hung/crashed (which is common while iterating). After a from-bootloader
  flash the user must **replug USB** to run the app (it doesn't auto-start). The
  port differs between bootloader and app modes — always auto-detect
  (`ls /dev/cu.usbmodem*`).
- **TFT_eSPI and rpcBLE cannot be in the same translation unit.** Arduino's
  `min()`/`max()` macros collide with rpcBLE's STL headers (`error: macro "min"
  passed 3 arguments`). Keep all BLE code in its own `.cpp` (`ble_bridge.cpp`)
  and never `#include` a BLE header from the same file as `TFT_eSPI.h`.
- **`#include <time.h>` explicitly** where you use `gmtime_r`/`struct tm`
  (`data.h`) — the Seeed SAMD core doesn't pull it in via `Arduino.h`.
- **`firmware/ble_probe/`** is a minimal BLE write-path diagnostic (NUS + an
  on-screen write counter, no FS/sprite). Flash it to confirm the radio/receive
  path on hardware when the full firmware misbehaves.
- **Diagnosing a blank screen:** *white* (or red "sprite alloc failed") = OOM at
  `createSprite` (RAM too tight). *Dark + blinking LED* = a hard fault, usually a
  boot-time global constructor (this is how the `Seeed_FS` `SD` ctor crash
  showed). Serial across a reset is racy (USB re-enumerates), so prefer on-screen
  signals (e.g. temporary full-screen color stages) over serial when bisecting a
  boot crash.
- **Usage endpoint request details:** `GET api.anthropic.com/api/oauth/usage`
  needs headers `Authorization: Bearer <token>`, `User-Agent: claude-code/<ver>`
  (without the claude-code UA you hit an aggressively-429'd bucket), and
  `anthropic-beta: oauth-2025-04-20`. Response `utilization` is **0–100** (not
  0–1) and `resets_at` is an ISO timestamp → convert to a countdown. It's
  undocumented and rate-limited; poll ≤ once/min and keep the local-estimate
  fallback.

## Conventions

- Firmware is single-sprite, single-threaded Arduino C++; keep RAM tight and
  avoid blocking calls in `loop()` (they break BLE). Match the existing terse
  `snprintf`-into-fixed-buffer style.
- Host is Python 3.9+, stdlib + bleak only, with `from __future__ import
  annotations` and `"X | None"` string annotations for 3.9 compatibility.
- Upstream artifacts (18 species, NUS protocol, `bufo` pack) are ported as-is;
  the Wio platform shim, split-view UI, usage panel, and bridge are this repo's
  additions.
