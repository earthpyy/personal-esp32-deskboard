# personal-esp32-deskboard

Desk dashboard on an ESP32-2432S028R ("Cheap Yellow Display" — 2.8" ILI9341 320x240 + XPT2046 resistive touch), plus a companion API for future board→server calls.

## Layout

- `apps/firmware` — PlatformIO + Arduino framework + LVGL 9 via [esp32_smartdisplay](https://github.com/rzeldent/esp32-smartdisplay). Board definitions are vendored as a git submodule in `apps/firmware/boards`.
- `apps/api` — [Elysia](https://elysiajs.com) on the Node adapter, TypeScript. Node 24 (pinned via `.node-version`, managed by mise), pnpm workspace. Serves today's Google Calendar events to the board (`GET /schedule`) and hosts the admin UI at `/admin`.
- `apps/admin` — Vue 3 + Vite + Tailwind 4 + daisyUI management SPA (Google accounts, settings, board status, manual sync). Built to static files served by the API.

## First-time setup

```bash
git clone --recurse-submodules <repo>
pnpm install
cd apps/firmware && python3 -m venv .venv && .venv/bin/pip install platformio
```

### Firmware secrets (`secrets.h`)

The firmware needs WiFi credentials and the API server address at compile time. Copy the committed template and fill in real values — the copy is gitignored, so it never leaves your machine:

```bash
cd apps/firmware
cp include/secrets.example.h include/secrets.h
```

Then edit `include/secrets.h`:

- `WIFI_SSID` / `WIFI_PASS` — your WiFi network (2.4 GHz; the ESP32 can't see 5 GHz).
- `API_BASE_URL` — where `apps/api` runs, e.g. `http://192.168.1.10:3300` (no trailing slash).

The build fails with a missing-header error until this file exists. Changing any value means reflashing (`pio run -t upload`).

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
pnpm dev:api                      # tsx watch on :3300
curl localhost:3300/health        # {"status":"ok"}
pnpm --filter api test            # unit tests (schedule builder + time helpers)
PORT=9000 pnpm dev:api            # override the port (default 3300)
```

`PORT` also works from `apps/api/.env`. Changing it means updating `API_BASE_URL` in the firmware's `secrets.h` (and reflashing) plus the redirect URI on the Google OAuth client. `pnpm dev:admin` reads the same `PORT` to point its proxy at the API.

### Google Calendar credentials (`.env`)

The API reads `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET` from the environment, or from an optional `apps/api/.env` (gitignored; real env vars take precedence):

```bash
cd apps/api
cp .env.example .env              # then fill in the two values
```

To get the values: create a Google Cloud project → enable the Calendar API → create an OAuth **web application** client with redirect URI `http://<server>:3300/api/oauth/callback` → set the consent screen to **In production** (in Testing mode, refresh tokens expire every 7 days). Then open `http://<server>:3300/admin` and connect your Google accounts from the Accounts page. Tokens and settings land in gitignored `apps/api/data/`.

The admin UI has no auth — it's LAN-only by design. Do not port-forward the API.

## Admin UI

```bash
pnpm dev:admin                    # Vite dev server, proxies /api and /schedule to :3300
pnpm build:admin                  # build into apps/admin/dist (served by the API at /admin)
```
