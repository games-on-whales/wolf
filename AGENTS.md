## What Wolf is

Wolf is a low-latency streaming server for [Moonlight](https://moonlight-stream.org/) that lets multiple
remote clients share a single Linux host to play games. Each client gets an on-demand virtual desktop
(Wayland compositor, no physical monitor needed) whose apps run in isolated Docker/Podman containers.
It implements the Moonlight protocol (pairing over HTTPS, RTSP handshake, ENet control channel, RTP
video/audio) and hands video/audio off to GStreamer pipelines. Linux + Docker first; C++20.

## Companion repositories

Wolf is split across three repos; the other two are integral and pulled in at build time — when
touching virtual input or virtual-display behavior, the real implementation often lives there:

- **[games-on-whales/inputtino](https://github.com/games-on-whales/inputtino)** — virtual input device
  library (`uinput`/`uhid`). Wolf uses it for gamepads (incl. gyro/accel) and pen/touch; mouse and
  keyboard don't go through it (see Architecture). Surfaced via `src/core/.../input.hpp`.
- **[games-on-whales/gst-wayland-display](https://github.com/games-on-whales/gst-wayland-display)** —
  the custom micro Wayland compositor (Rust, built on [Smithay](https://github.com/Smithay/smithay);
  we track a fork at [games-on-whales/smithay](https://github.com/games-on-whales/smithay)). Creates
  on-demand desktops and exposes the raw framebuffer as a GStreamer plugin + C API. Installed as
  `libgstwaylanddisplay` (surfaced via `src/core/.../virtual-display.hpp`); its `.so` must be on
  `GST_PLUGIN_PATH` at runtime and to run the tests.

Prebuilt guest-app containers live in [games-on-whales/gow](https://github.com/games-on-whales/gow).

## Build & test

Two supported paths, both documented in `docs/modules/dev/pages/manual_build.adoc`:

- **Devcontainer (recommended)** — `docker/wolf.Dockerfile` target `wolf-builder` via `.devcontainer/`,
  so you build in the exact environment of the official image with all deps preinstalled (VS Code:
  *Dev Containers: Clone Repository in Container Volume*, pick the Clang kit).
- **Manual host build** — build Wolf outside Docker (Docker must still be installed for Wolf to do
  anything useful). The doc covers building GStreamer and `gst-wayland-display` from source, apt deps,
  the required `LD_LIBRARY_PATH`/`PKG_CONFIG_PATH`/etc. env, and a `runwolf.sh` template of `WOLF_*`
  runtime vars.

Most C++ deps are fetched at configure time via CMake `FetchContent` (fmt, tomlplusplus, reflect-cpp,
eventbus, Catch2, immer, boost via `BoostLoader`); system libs still needed include Boost, GStreamer,
Wayland, libinput, libevdev, libudev, OpenSSL, PulseAudio, libdrm, libpci.

```bash
# Configure (matches CI). CI uses C++20; manual_build.adoc still shows 17 — prefer 20.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 -DCATCH_DEVELOPMENT_BUILD=ON

ninja -C build wolf         # server binary → build/src/moonlight-server/wolf
ninja -C build wolftests    # test suite (Catch2)
cd build/tests && ./wolftests
```

Run a single test / subset with Catch2 selectors: `./wolftests "test name"`, `-c "section"`,
`"[tag]"`, or `--list-tests`. Tests need `HOST_APPS_STATE_FOLDER`, `GST_PLUGIN_PATH` (where
`libgstwaylanddisplay` is installed), `XDG_RUNTIME_DIR`, and `RUST_LOG` set (see the `test` job in
`.github/workflows/linux-build-test.yml`).

Hardware/environment-dependent tests are gated by CMake options so CI can skip what a runner lacks:
`TEST_DOCKER`, `TEST_NVIDIA`, `TEST_VIRTUAL_INPUT`, `TEST_UHID`, `TEST_RUST_WAYLAND`, `TEST_EXCEPTIONS`,
`TEST_SDL`. CI runs a g++/clang matrix with `BUILD_SHARED_LIBS` both ON and OFF — keep both working.

C++ is auto-formatted with clang-format (`.clang-format`, 120 col) and enforced in CI; run
`clang-format -i` on changed files before committing. `.clang-tidy` is also present.

## Architecture

**Style (deliberate, per the original author):** functional — no global state, side effects avoided,
immutable inputs → new outputs. Shared/persistent state lives in [immer](https://github.com/arximboldi/immer)
persistent containers wrapped in `immer::atom<...>` (e.g. `SessionsAtoms`, `PairedClientList`): treat a
snapshot as read-only and swap in a new one to update, never mutate in place. This is what makes Wolf's
heavy concurrency (many simultaneous users) lock-free and safe. Prefer pure functions over shared
mutable state, and keep protocol logic decoupled from the server runtime.

**Coordination:** components talk through a shared **event bus** (`dp::event_bus`, `EventBusType` in
`events/events.hpp`) rather than direct calls — `PairSignal`, `StreamSession`, `CreateLobbyEvent`,
`StartRunner`, `PlugDeviceEvent`, docker lifecycle events, etc. For cross-component behavior, define/
handle an event instead of adding a direct dependency. New events go in `events/events.hpp`;
`reflectors.hpp` exposes them for serialization (all config/API/event serialization uses reflect-cpp —
annotate types rather than hand-writing parsers).

### How a stream works

See `docs/modules/dev/pages/how-it-works.adoc` for the full picture.

- **Virtual desktop** — `gst-wayland-display` creates a desktop on demand and feeds its raw framebuffer
  into the encode pipeline. Our images run [Sway](https://swaywm.org/) (optionally
  [Gamescope](https://github.com/ValveSoftware/gamescope)) as a Wayland client inside it. The
  compositor has no XWayland; apps needing X (e.g. Steam) rely on Gamescope for it.
- **Virtual audio** — by default PulseAudio runs **inside the Wolf container** under supervisord
  (`docker/startup.sh` sets `WOLF_EMBED_PULSE=true`; Wolf waits for the PA socket before starting). With
  an external `PULSE_SERVER`, or if `pulseaudio` isn't installed, Wolf falls back to the legacy
  standalone `WolfPulseAudio` sidecar. Either way it uses `libpulse` for per-session virtual sinks.
- **Virtual input** — mouse/keyboard events go **directly to the Wayland compositor** (no host device);
  gamepads and pen/touch are real `uinput`/`uhid` devices created by `inputtino`. Those are visible on
  the host, so `85-wolf.rules` restricts them to a group/seat for isolation. The virtual DualSense uses
  `uhid` (plain `uinput` wasn't enough) — see the author's blog:
  [1](https://abeltra.me/blog/inputtino-uhid-1/),
  [2](https://abeltra.me/blog/inputtino-uhid-2/),
  [3](https://abeltra.me/blog/inputtino-uhid-3/).
- **Hotplug** — devices added mid-stream are injected into the running container via the `src/fake-udev`
  CLI; see [Docker hotplug](https://abeltra.me/blog/docker-hotplug/) for how fake-udev achieves this.
- **Guest apps** — run in containers. The Docker runner (`runners/docker.cpp`) builds the per-session
  spec: mounts a per-app state folder, exposes the GPU render node (`/dev/dri/renderD*`), on NVIDIA adds
  the driver (custom driver volume, or `--gpus all` + `NVIDIA_VISIBLE_DEVICES`/`_DRIVER_CAPABILITIES` +
  the `nvidia` runtime), passes through Wolf's virtual input devices, sets `DeviceCgroupRules` for the
  dynamic `hidraw`/`input` majors (needed for the virtual DualSense), and wires up fake-udev. It then
  blocks for the container's lifetime plugging/unplugging devices via the event bus, and on exit
  stops/removes it (`WOLF_STOP_CONTAINER_ON_EXIT`) and cleans up the udev scratch dir.
- **Streaming** — GStreamer encodes video/audio (HW accel via CUDA/QuickSync/VAAPI; the whole pipeline
  is a config-string in `config.toml`, overridable without code). Custom plugins in `gst-plugin/`
  (`rtpmoonlightpay_video`/`_audio`) split, RTP-encode, and add FEC to Moonlight's format. The pipeline
  is zero-copy from framebuffer to encoded frames — on by default, disable with `WOLF_USE_ZERO_COPY=FALSE`
  (auto-falls back to legacy when an encoder can't support it); see
  [The road to zero-copy in Wolf](https://abeltra.me/blog/road-to-zero-copy-in-wolf/).

### Code map

`CMakeLists.txt` composes these targets:

- **`src/moonlight-protocol`** — platform-agnostic, mostly stateless Moonlight library: HTTP/S
  `protocol.hpp`, control-packet `control.hpp`, Reed-Solomon `fec.hpp` (on
  [nanors](https://github.com/sleepybishop/nanors)), and an RTSP parser built from a PEG grammar via
  [cpp-peglib](https://github.com/yhirose/cpp-peglib). No server deps.
- **`src/core`** (`wolf::core`) — reusable platform abstractions: `docker.hpp` (Docker/Podman REST over
  libcurl + boost::json), `input.hpp` (inputtino), `virtual-display.hpp` (gst-wayland-display),
  `audio.hpp` (libpulse), `gstreamer.hpp`. Split into `platforms/{all,linux,unknown}`; `unknown` holds
  no-op stubs so non-Linux configs still compile — provide one when adding platform code.
- **`src/gst-video-context`** — GStreamer video context helpers.
- **`src/fake-udev`** — CLI (Linux only) that generates/injects udev events (hotplug, above).
- **`src/moonlight-server`** (`wolf::runner` → the `wolf` binary) — the full server:
  - `wolf.cpp` — `main`: loads config, sets up certs, starts servers, wires the event bus, runs the
    boost::asio loop, graceful shutdown via a signal flag.
  - `state/` — `AppState` + config model (TOML via `configTOML.cpp`, tomlplusplus + reflect-cpp; see
    `tests/assets/config.test.toml`).
  - `rest/` `control/` `rtsp/` `rtp/` — the Moonlight protocol stack: HTTPS pairing/REST, RTSP setup,
    ENet control channel + input handling, RTP ping/transport.
  - `streaming/` `gst-plugin/` `audio/` — GStreamer video/audio; `audio/pulse_router` bridges container
    audio.
  - `runners/` — `docker.cpp` (containers, default) and `process.cpp` (host process), fired by
    `StartRunner`.
  - `sessions/` — session/lobby lifecycle (`moonlight.cpp`, `lobbies.cpp`); a "lobby" lets clients
    share/join a running desktop.
  - `api/` — a separate control API over a Unix socket (`unix_socket_server.cpp`) with an OpenAPI spec
    (`openapi.cpp`), used by external tools (e.g. wolf-ui). Distinct from the Moonlight-facing `rest/`.

## Runtime configuration (env vars)

Behavior is driven by `WOLF_*` env vars read via `utils::get_env` (full working set in `wolf.cpp` and
`.devcontainer/devcontainer.json`): `WOLF_CFG_FILE`, `WOLF_PRIVATE_KEY_FILE`/`WOLF_PRIVATE_CERT_FILE`,
`WOLF_LOG_LEVEL`, `WOLF_DOCKER_SOCKET`, `WOLF_RENDER_NODE`/`WOLF_ENCODER_NODE` (GPU DRI nodes),
`WOLF_PULSE_IMAGE`, `WOLF_INTERNAL_IP`/`WOLF_INTERNAL_MAC`, `WOLF_USE_ZERO_COPY`,
`WOLF_STOP_CONTAINER_ON_EXIT`. A few (e.g. `WOLF_EMBED_PULSE`, `PULSE_SERVER`) are set/consumed by
`docker/startup.sh` + `supervisord.conf`, not by Wolf itself.