# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A pnpm monorepo for a desk dashboard running on an ESP32-2432S028R "Cheap Yellow Display" (2.8" ILI9341 320x240 TFT + XPT2046 resistive touch, CH340 USB-serial):

- `apps/firmware` — PlatformIO project (Arduino framework) using esp32_smartdisplay + LVGL 9. Board JSONs are a git submodule at `apps/firmware/boards` (clone with `--recurse-submodules`). Copy `include/secrets.example.h` to `include/secrets.h` (gitignored) before building.
- `apps/api` — Elysia server on the Node adapter (`@elysiajs/node`), TypeScript, port 3300. Serves `GET /schedule` (today's Google Calendar events, merged across accounts) for the board, `/api/*` JSON for the admin UI, and the built admin SPA at `/admin`. OAuth tokens + settings live in gitignored `apps/api/data/`.
- `apps/admin` — Vue 3 + Vite + Tailwind 4 + daisyUI management SPA (accounts, settings, board status, manual sync). Built to static files served by the API.

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
pnpm dev:api                      # tsx watch, listens on :3300
pnpm dev:admin                    # Vite dev server, proxies /api and /schedule to :3300
pnpm build:admin                  # build SPA into apps/admin/dist (served at /admin)
pnpm --filter api test            # unit tests (node:test) for schedule builder + time helpers
curl localhost:3300/health        # {"status":"ok"}
```

Firmware verification = clean build + flash + observing serial log / on-screen behavior.

## Schedule feature

- `GET /schedule` returns a flat JSON the board renders verbatim: `now`/`start`/`end` are epoch seconds, `time`/`now_label`/`date` are pre-formatted server-side in the configured timezone. The board has **no NTP and no timezone logic** — it anchors its clock to `now` per poll (every 60 s) and uses the last `now_label` for the stale "as of HH:MM" header. Events are pre-sorted, clamped to today, capped (30 timed / 10 all-day), declined-filtered, colored (event override, else calendar color).
- Google fetches are cached (default 5 min, settable in the admin UI); the schedule itself is rebuilt per request so `now` is always current. Cache also busts on day rollover and via `POST /api/sync`.
- API env vars: `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET`, plus optional `PORT` (default 3300; `apps/admin/vite.config.ts` reads the same var so its proxy follows). The port is baked into the board's `API_BASE_URL` and the OAuth redirect URI, so changing it means reflashing + re-registering. The OAuth redirect URI `http://<host>:3300/api/oauth/callback` must be registered on the Google Cloud OAuth client (web application type), and the consent screen must be **In production**, otherwise refresh tokens expire after 7 days.
- The admin UI has no auth — LAN-only by design; never port-forward the API.
- Firmware: the HTTP fetch task (`src/net/schedule_client.cpp`) is pinned to core 0 and hands data to the UI via a mutex + fresh flag — never call LVGL from it. LVGL runs on core 1 with `loop()`.

## Claude usage feature

- `GET /claude` returns flat JSON the board renders verbatim (same anchor-on-`now` contract as `/schedule`): per-account `five_hour`/`weekly` with `percent`, `severity` (`normal`/`warning`/`critical`), and a pre-formatted `resets_label` in the configured timezone. Shown on the board's Claude tab (`src/ui/page_claude.cpp`) as stacked per-account panels with two severity-colored bars each.
- Accounts are managed in the admin **Claude** tab (add/edit/delete) and persisted to gitignored `apps/api/data/claude-accounts.json` (`{id, alias, configDir}`); `apps/api/src/claudeAccounts.ts` is the store, `claude.ts` reads it per fetch. `configDir` is a `CLAUDE_CONFIG_DIR` (may be `~`-prefixed); **empty `configDir` = the default `~/.claude` profile**.
- The **live OAuth token is read from the macOS Keychain** — service `Claude Code-credentials-<first8 sha256(expanded configDir)>` (or the bare `Claude Code-credentials` when `configDir` is empty), account = OS user — via the `security` CLI. Needs a one-time "Always Allow" grant per item. A profile's `.credentials.json` file is deliberately ignored (Claude Code doesn't keep it fresh; the Keychain is the live store on macOS). If a profile is in *file mode* (its `.credentials.json` exists) its Keychain copy goes stale → the account reads `expired`; remove that file and `claude /login` to restore Keychain mode.
- Data source: `GET https://api.anthropic.com/api/oauth/usage`, headers `Authorization: Bearer`, `anthropic-beta: oauth-2025-04-20`, `anthropic-version: 2023-06-01`, and **`User-Agent: claude-code/<v>` (required — omitting it yields persistent 429s)**. The endpoint rate-limits hard, so results are cached (`claudeCacheTtlMinutes`, default 5) even though the board polls `/claude` every 60 s; a transient 429/network error keeps the last-good numbers instead of flapping to "unavailable". Cache busts on `POST /api/sync`.
- Read-only by design: no token refresh, no write-back. An expired/unreadable token shows a dimmed "unavailable" panel until Claude Code itself refreshes that account.
- Admin **Claude** tab (`apps/admin/src/pages/ClaudePage.vue`) shows the usage bars + a "Claude sync" card (last-sync time + per-account ok/error) with a manual **Sync now** (`POST /api/claude/sync`, forces a refresh). `GET /api/claude/sync` returns the last fetch outcome — the raw per-account result, not the last-good fallback that the board bars use.
- Tab icon is a hand-rasterized Claude "spark" one-glyph font (`src/ui/font_claude.c`, U+E000) — same one-glyph-font pattern as `font_calendar.c`, but bitmap-authored (no lv_font_conv source), so don't try to regenerate it from a TTF.

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
- Thai text renders via `src/ui/noto_thai_{14,12}.c` (Noto Sans Thai, Thai block only), set as `LV_FONT_DEFAULT` / used in `page_schedule.cpp`. Each is the primary font with `.fallback` pointing at the matching `lv_font_montserrat_*`, so Latin and `LV_SYMBOL_*` glyphs (absent from the Thai font) render through Montserrat unchanged. **The `.fallback` line is a MANUAL EDIT** — `lv_font_conv` emits `.fallback = NULL`, so after regenerating (command + re-edit are documented in each file's header) you must re-set it, or all Latin/symbols turn to tofu. LVGL 9.2 has no Thai shaping engine; marks stack correctly only because Noto's combining glyphs carry zero advance + negative x-bearing.

## Repo conventions

- Everything is project-local by design: PlatformIO in `apps/firmware/.venv`, Node via mise + `.node-version`. Don't install global tools.
- `.zed/tasks.json` defines build/upload/monitor/api tasks for the Zed editor.
