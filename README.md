# personal-esp32-deskboard

Desk dashboard on an ESP32-2432S028R ("Cheap Yellow Display" — 2.8" ILI9341 320x240 + XPT2046 resistive touch), plus a companion API for future board→server calls.

## Layout

- `apps/firmware` — PlatformIO + Arduino framework + LVGL 9 via [esp32_smartdisplay](https://github.com/rzeldent/esp32-smartdisplay). Board definitions are vendored as a git submodule in `apps/firmware/boards`.
- `apps/api` — [Elysia](https://elysiajs.com) on the Node adapter, TypeScript. Node 24 (pinned via `.node-version`, managed by mise), pnpm workspace.

## First-time setup

```bash
git clone --recurse-submodules <repo>
pnpm install
cd apps/firmware && python3 -m venv .venv && .venv/bin/pip install platformio
```

## Firmware (run from `apps/firmware`)

```bash
.venv/bin/pio run                 # build
.venv/bin/pio run -t upload       # flash over USB
.venv/bin/pio device monitor      # serial monitor (115200)
.venv/bin/pio run -t compiledb    # regenerate compile_commands.json for clangd
```

Zed users: these are also available as tasks (`.zed/tasks.json`), and `apps/firmware/.clangd` makes clangd work with the generated `compile_commands.json`.

### Version pins (do not bump casually)

`platformio.ini` pins `esp32_smartdisplay@2.1.1` + `lvgl@9.2.2`, and the `boards/` submodule is pinned to the commit that release ships with (`d8a9270`). These three move in lockstep:

- the registry's esp32_smartdisplay 3.0.0 is an unreleased snapshot that compiles against no current LVGL release (`sw_rotate` API);
- LVGL 9.2.x requires the `LV_CONF_PATH` build flag **unquoted**, ≥9.3 requires it quoted;
- newer boards-repo commits rename the SPI config flags for the 3.x snapshot and break 2.1.1.

To upgrade later: pick an esp32_smartdisplay GitHub release, match its `library.json` LVGL range, and check out `boards/` at the submodule commit recorded in that release tag.

### Board revision fallbacks

If the screen stays blank or colors/mirroring look wrong, flash one of the sibling envs instead: `-e esp32-2432S028Rv2` (USB-C revision, different init sequence) or `-e esp32-2432S028Rv3` (dual-USB clone with ST7789 panel).

## API

```bash
pnpm dev:api                      # tsx watch on :3000
curl localhost:3000/health        # {"status":"ok"}
```
