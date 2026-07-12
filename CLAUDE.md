# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A pnpm monorepo for a desk dashboard running on an ESP32-2432S028R "Cheap Yellow Display" (2.8" ILI9341 320x240 TFT + XPT2046 resistive touch, CH340 USB-serial):

- `apps/firmware` — PlatformIO project (Arduino framework) using esp32_smartdisplay + LVGL 9. Board JSONs are a git submodule at `apps/firmware/boards` (clone with `--recurse-submodules`).
- `apps/api` — Elysia server on the Node adapter (`@elysiajs/node`), TypeScript, currently just `GET /health` on port 3000. Intended for future calls from the board.

## Commands

Firmware (run from `apps/firmware`; PlatformIO lives in a project-local venv, there is no global `pio`):

```bash
.venv/bin/pio run                 # build
.venv/bin/pio run -t upload       # flash over USB (device shows as /dev/cu.usbserial-*)
.venv/bin/pio device monitor      # serial monitor, 115200 baud (needs a real TTY)
.venv/bin/pio run -t compiledb    # regenerate compile_commands.json for clangd
```

API (from repo root; Node 24 pinned via `.node-version`, mise-managed):

```bash
pnpm dev:api                      # tsx watch, listens on :3000
curl localhost:3000/health        # {"status":"ok"}
```

There are no automated tests yet. Firmware verification = clean build + flash + observing serial log / on-screen behavior.

## Critical constraint: firmware version pins move in lockstep

`platformio.ini` pins `esp32_smartdisplay@2.1.1` + `lvgl@9.2.2`, and the `boards/` submodule is checked out at `d8a9270` (the commit that release ships with). Do not bump any of these independently:

- Registry `esp32_smartdisplay@3.0.0` is an unreleased snapshot; it compiles against no current LVGL release (`sw_rotate` API).
- LVGL 9.2.x requires the `LV_CONF_PATH` build flag **unquoted** (it stringifies it itself); LVGL ≥9.3 requires it quoted. The flag in `platformio.ini` is unquoted on purpose.
- Newer `boards/` commits rename the panel/touch SPI config flags (`*_DC_AS_CMD_PHASE` → `*_DC_LOW_ON_DATA`) for the 3.x snapshot and break 2.1.1.

To upgrade: pick an esp32_smartdisplay GitHub release, use the LVGL range from its `library.json`, and check out `boards/` at the submodule SHA recorded in that release tag (`GET /repos/rzeldent/esp32-smartdisplay/contents/boards?ref=<tag>`). After changing LVGL versions, regenerate `include/lv_conf.h` from the new `.pio/libdeps/*/lvgl/lv_conf_template.h` (set the `#if 0` guard at the top to `#if 1`; keep `LV_COLOR_DEPTH 16`).

## Firmware specifics

- `platformio.ini` has three envs for CYD hardware revisions; `esp32-2432S028R` is the default. If a flashed board shows a blank/mirrored/wrong-color screen, try `-e esp32-2432S028Rv2` (USB-C revision) or `-e esp32-2432S028Rv3` (ST7789 clone) before debugging code.
- `loop()` must keep the `lv_tick_inc(millis() - last) + lv_timer_handler()` pattern; LVGL has no other tick source here.
- `apps/firmware/.clangd` strips xtensa-gcc flags clangd doesn't understand — needed for editor IntelliSense (Zed) against the generated `compile_commands.json`.

## Repo conventions

- Everything is project-local by design: PlatformIO in `apps/firmware/.venv`, Node via mise + `.node-version`. Don't install global tools.
- `.zed/tasks.json` defines build/upload/monitor/api tasks for the Zed editor.
