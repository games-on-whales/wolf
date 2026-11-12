# Running Wolf under rootless Podman — practical guide

Working single-host setup (podman rootless, user `wolf`), verified 2026-11-06:
pairing, video/audio streaming, app containers, and a physical 8BitDo
controller all work through the whole chain.

**Core problem:** Wolf runs rootless but spawns app containers (Steam, GOW
runners, …) through the podman socket, and those containers need kernel input
device access. Nearly every pitfall below stems from that.

## Layout

| What | Where |
|---|---|
| Wolf source checkout | `~/src/wolf` (branch `beta`, remote `games-on-whales/wolf`) |
| Custom image | `localhost/wolf:custom` |
| Compose project | `~/containers/wolf/docker-compose.yml` (`podman-compose`) |
| Wolf config | `/wolf/etc/wolf/cfg/config.toml` (bind-mounted to `/etc/wolf`) |
| wolf.sock | `/var/run/wolf/wolf.sock` (host + containers) |
| Podman socket | `/run/user/1013/podman/podman.sock` → mounted into wolf as `/var/run/docker.sock` (ro) |

## Source patches (required — stock Wolf fails under rootless)

Three patches live in the checkout:

1. **`DeviceCgroupRules` opt-out** (`src/moonlight-server/runners/docker.cpp`):
   rootless Podman does not support `HostConfig.DeviceCgroupRules`, so
   container create fails. The whole block is gated behind an env var — set in
   compose:
   ```yaml
   environment:
     WOLF_DISABLE_DEVICE_CGROUP_RULES: "1"
   ```
   Unset = stock upstream behaviour, so the patch is non-breaking for
   rootful/docker deployments.
2. **Tolerant `mknod`/`chmod`** (same file): Wolf pushes a fake-udev command
   (`mkdir -p /dev/input && mknod … && chmod 777 … && fake-udev …`) into app
   containers; `mknod` for an existing host-provided node fails in a rootless
   user namespace and aborted the whole command. Patch makes both non-fatal
   (`|| true`) — the nodes already exist via the `/dev/input` bind mount.
3. **Wayland-socket wait guard** (`src/moonlight-server/sessions/moonlight.cpp`):
   only wait for the wayland socket if the runner actually returned a socket
   name (`if (!ready.wayland_socket_name.empty() && !wait_for_wayland_socket(...))`),
   otherwise runners with no socket name hit an unconditional wait that could
   abort startup.

## Host (root) changes — NOT installed by any package!

Re-create these by hand on a fresh host (requires root), then
`udevadm control --reload && udevadm trigger`.

**`/etc/udev/rules.d/99-wolf-input.rules`** (also `groupadd wolf-input` and
`usermod -aG wolf-input wolf`):

```
KERNEL=="uinput", GROUP="wolf-input", MODE="0660"
KERNEL=="uhid", GROUP="wolf-input", MODE="0660"
```

Lets the rootless wolf container open `/dev/uinput`/`/dev/uhid` (passed
through via `devices:` in compose).

**`/etc/udev/rules.d/99-wolf-virtual-input.rules`**: Wolf's virtual pads
appear as host `/dev/input/event*` nodes owned `root:input` `0660`. App
containers are separate rootless user namespaces — their "root" is not the
host's root, so `0660` is unreadable to them. Widen **only** wolf-created
devices:

```
SUBSYSTEM=="input", ATTRS{name}=="Wolf *virtual*", MODE="0666"
```

Verify: `ls -l /dev/input/eventX` → `crw-rw-rw- … Wolf … (virtual) pad`.

> Do not install the repo's `85-wolf.rules` alongside these two — last rule
> wins for `MODE`/`GROUP`.

## docker-compose.yml (wolf service)

```yaml
image: localhost/wolf:custom
ipc: host
environment:
  WOLF_SOCKET_PATH: /var/run/wolf/wolf.sock
  WOLF_RENDER_NODE: /dev/dri/renderD129
  WOLF_STOP_CONTAINER_ON_EXIT: "FALSE"
  WOLF_USE_ZERO_COPY: "FALSE"
  WOLF_DISABLE_DEVICE_CGROUP_RULES: "1"
devices:
  - /dev/uinput:/dev/uinput
  - /dev/uhid:/dev/uhid
  - /dev/dri:/dev/dri
  - /dev/fuse:/dev/fuse
security_opt:
  - label=disable                 # SELinux: don't relabel these
volumes:
  - /run/user/1013/podman/podman.sock:/var/run/docker.sock:ro,z
  - /wolf/etc/wolf:/etc/wolf:rw,z
  - /var/run/wolf:/var/run/wolf:rw,z
  - /dev:/dev:rw                  # full devfs (rootless: no cgroup device rules to fall back on)
  - /run/udev:/run/udev:rw        # wolf's udev monitor / fake-udev
```

Recreate: `cd ~/containers/wolf && podman-compose up -d`
(config-only restart: `podman restart wolf`).

## config.toml

### `/dev/input` bind mounts

Add to the Wolf UI base profile and both Steam/GOW runner profiles:

```toml
mounts = ['/dev/input:/dev/input:rw']
```

(the UI profile also mounts `/var/run/wolf/wolf.sock`). Together with the
fake-udev fallback and the udev rule above this gives readable, `0666`,
correctly-named nodes inside every app container.

### GOW `GOW_REQUIRED_DEVICES` (boot-killer)

Default glob `/dev/input/*` also matches the `by-path` **directory**, and
GOW's `15-setup_devices.sh` then `chmod`s it inside the container →
`Operation not permitted` → RUN_SWAY apps abort at boot. Fix in **all 7 app
profiles**:

```diff
- GOW_REQUIRED_DEVICES=/dev/input/* /dev/dri/* /dev.nvidia*
+ GOW_REQUIRED_DEVICES=/dev/input/event* /dev/dri/* /dev.nvidia*
```

### Editing rules

- **Hand-edit + `podman restart wolf`**. Avoid
  `POST /api/v1/clients/settings`: it persists via `rfl::toml::save`, which
  rewrites the whole file (comments/formatting lost; values survive).
- **Never hand-merge/replace `[[paired_clients]]` blocks**: each client keeps
  its own `app_state_folder` (preserve the per-client Moonlight/Steam state),
  and `controllers_override` is **positional** (entry `[0]` = controller pad 1).
- Settings are snapshotted into the session at launch → relaunch the app after
  a change.
- Keep the `config.toml.bak*` / `config.toml.pre-*` backups — they are the
  rollback path.

## Building & deploying the custom image

```bash
podman build --pull=always --target runner -f docker/wolf.Dockerfile -t localhost/wolf:custom .
cd ~/containers/wolf && podman-compose up -d
```

Sanity after deploy:

```bash
podman logs --since 60s wolf | grep -E 'Reading config|HTTP server|ERROR'
curl -s --unix-socket /var/run/wolf/wolf.sock http://localhost/api/v1/sessions
```

## Controller face-buttons (8BitDo + Moonlight Android)

- The Moonlight client sends **label-based** (printed letter) button bits, not
  positional.
- `controllers_override` (`XBOX` vs `NINTENDO`) only changes the pad's
  identity / inputtino table — with a label-aware consumer the in-app result
  is identical, so **the override cannot fix the mapping**.
- Steam's "Use Nintendo Button Layout" and Moonlight's "Flip face buttons" both
  swap **both** A/B *and* X/Y — wrong granularity for full Xbox-positional
  behavior.
- Current working state: `controllers_override = ["NINTENDO"]` on all paired
  clients; the Steam toggle decides which pair comes out right.
- Remaining options if you want full Xbox-positional behavior:
  1. Env-gated A/B-only swap patch in wolf source (all apps, rebuild needed),
  2. Steam per-controller custom button layout (Steam only, zero code),
  3. Put the 8BitDo in positional/X-input mode (fixes at the source).

## Rollback

1. Config: `cp config.toml.<backup> config.toml && podman restart wolf`
2. Compose: `cp docker-compose.yml.bak3 docker-compose.yml && podman-compose up -d`
3. udev: delete `/etc/udev/rules.d/99-wolf-{input,virtual-input}.rules`,
   `udevadm control --reload && udevadm trigger` (optionally re-install
   `85-wolf.rules` for the desktop-host variant)
4. Source: revert the three patched files, rebuild the image, or fall back to
   `ghcr.io/games-on-whales/wolf:stable` in compose.
