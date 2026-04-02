# Wolf Cloud Gaming Setup

This directory contains `wolf.sh`, the entry point script that turns a Linux machine with a GPU into a cloud gaming host using [Wolf](https://github.com/games-on-whales/wolf) and [Moonlight](https://moonlight-stream.org/). You only need `wolf.sh` to get started -- it automatically downloads any additional scripts it needs.

## What is this?

**Wolf** is a program that lets you play games on a powerful computer (your server) while viewing and controlling them from another device -- your phone, tablet, laptop, or TV. Think of it like Netflix, but for games: the server does all the hard work, and you just watch and play over your network.

**Moonlight** is the app you install on the device you want to play on (the "client"). It connects to Wolf and shows you the game.

**Wolf Den** is a web interface that makes it easy to manage Wolf -- you can pair new devices, add apps, and configure settings from your browser. It's deployed automatically alongside Wolf on all platforms.

## What does this script do?

`wolf.sh` detects what kind of system you're running and sets everything up automatically:

| Environment | What happens |
|---|---|
| **Proxmox** (a server hypervisor) | Creates a container, passes your GPU into it, installs Docker, and deploys Wolf + Wolf Den inside |
| **LXC / Incus** (Linux container tools) | Same as Proxmox but using standalone LXC tools or Incus instead |
| **Unraid** (NAS server OS) | Deploys Wolf + Wolf Den via Docker Compose with persistent appdata paths and boot-persistent udev rules |
| **TrueNAS SCALE** (NAS server OS) | Deploys Wolf + Wolf Den via Docker Compose on a ZFS dataset with update-persistent init scripts |
| **NixOS** (declarative Linux distro) | Generates a NixOS module (`wolf.nix`) that you import into your system configuration |
| **Docker** (on any Linux machine) | Deploys Wolf + Wolf Den directly using Docker Compose |
| **Podman** (on any Linux machine) | Deploys Wolf + Wolf Den as systemd services using Podman Quadlets |

You don't need to choose -- the script figures out which one you have and does the right thing. It checks in this order: Proxmox, LXC/Incus, Unraid, TrueNAS, NixOS, Podman, Docker.

> **What's a "container"?** On Proxmox, LXC, and Incus, the script creates a lightweight virtual environment (a "container") on your server to run Wolf in. Think of it as a mini computer running inside your computer. Your GPU is shared with this container so Wolf can use it for gaming. On Docker and Podman, Wolf runs directly on your machine without this extra layer.

After setup, **Steam** is pre-configured as a launchable app in Moonlight. You can add more apps (like other game launchers or desktop environments) through Wolf Den or by editing the Wolf config file.

## What you need before starting

### On the server (the computer that will run the games)

- **A Linux machine with a GPU** -- this can be:
  - A Proxmox VE server (version 7.x or 8.x)
  - Any Linux machine with LXC tools (`lxc-create`) or Incus installed
  - An Unraid server (6.x or 7.x) with Docker enabled
  - A TrueNAS SCALE server (Electric Eel 24.10+) with Docker support
  - A NixOS machine (with or without flakes)
  - Any Linux machine with Docker installed
  - Any Linux machine with Podman installed
- **GPU drivers installed and working**:
  - **Intel** -- usually works out of the box (driver loads automatically)
  - **AMD** -- usually works out of the box (driver loads automatically)
  - **NVIDIA** -- you must install the proprietary NVIDIA driver yourself first
- **Root access** -- you need to run the script as root (using `sudo`)

### On the client (the device you want to play on)

- **The Moonlight app** -- download it for free from [moonlight-stream.org](https://moonlight-stream.org/)
  - Available for: Windows, Mac, Linux, Android, iOS, Apple TV, Raspberry Pi, and more

### Proxmox only (extra requirement)

- **A Debian 13 LXC template** downloaded in Proxmox (the script expects `debian-13-standard_13.1-2_amd64.tar.zst` in your ISO storage)

### How to check if your GPU driver is loaded

Open a terminal on your server and run:

```bash
ls /dev/dri/renderD*
```

If you see output like `/dev/dri/renderD128`, your GPU driver is loaded and you're good to go. If you see nothing, you need to install GPU drivers first -- search for "[your Linux distro] install [Intel/AMD/NVIDIA] GPU driver" for instructions.

## Quick start

### Step 1: Download and run

SSH into your server and run:

```bash
ssh root@your-server-ip
curl -fsSLO https://raw.githubusercontent.com/games-on-whales/wolf/stable/quickstart/wolf.sh
chmod +x wolf.sh
./wolf.sh
```

Or, if you don't have curl:

```bash
wget -qO wolf.sh https://raw.githubusercontent.com/games-on-whales/wolf/stable/quickstart/wolf.sh
chmod +x wolf.sh
./wolf.sh
```

The script will:
- Figure out if you're on Proxmox, LXC, Incus, Unraid, TrueNAS, Docker, or Podman
- Download the scripts it needs for your environment (requires curl or wget)
- Detect your GPU automatically
- Set everything up

### Step 3: Read the output

When it finishes, you'll see a summary with your server's IP and instructions. For example:

```
================================================================
Wolf cloud gaming is deployed (Docker).

  Wolf:      streaming on ports 47984-48200 (Moonlight)
  Wolf Den:  http://192.168.1.50:8080 (web management)
  Compose:   /opt/wolf/docker-compose.yml
  GPU:       AMD Radeon RX 6900 XT (amdgpu) at /dev/dri/renderD128

To pair with Moonlight:
  1. Open Wolf Den at http://192.168.1.50:8080 to manage apps and clients
  2. Open Moonlight and add server: 192.168.1.50
  3. Enter the pairing PIN shown in Moonlight into Wolf Den
================================================================
```

### Step 4: Connect with Moonlight

1. Open the **Moonlight** app on your phone/tablet/laptop/TV
2. Tap **Add Host** (or the `+` button) and type in the server IP shown in the output
3. Moonlight will show a **4-digit PIN** -- enter it into **Wolf Den** (the web management page shown in the output) to complete pairing
4. You're connected -- launch a game from Moonlight

## Options

All options are optional. Running `./wolf.sh` with no options works in most cases -- the script auto-detects sensible defaults for everything. You pass options to `wolf.sh` and it forwards them to the appropriate environment script. Flags that don't apply to your detected environment are silently ignored, so the same command line works everywhere.

### All environments

These options are recognized by every environment script.

| Option | What it does | Default |
|---|---|---|
| `--render-node <path>` | Force a specific GPU render device (e.g. `/dev/dri/renderD128`). Without this, the script auto-detects your GPU(s) and, if there are multiple, presents a selection prompt. | Auto-detected |

### Container environments (Proxmox, LXC, Incus)

These options control the container that gets created to run Wolf in. They have no effect on Docker, Podman, Unraid, or TrueNAS (which run Wolf directly on the host).

| Option | What it does | Default |
|---|---|---|
| `--cpu <cores>` | Number of CPU cores to allocate to the container. More cores help with demanding games. | `4` |
| `--ram <mb>` | Amount of memory in megabytes to allocate to the container. | `4096` (4 GB) |
| `--disk <gb>` | Root disk size in gigabytes for the container. This is the disk inside the container where Docker, Wolf, and game data live. If you plan to install many games via Steam, increase this. | `16` |
| `--name <name>` | Name for the container. Used by LXC and Incus only (Proxmox uses the hostname `wolf` and identifies containers by CTID instead). | `wolf` |

### Proxmox only

These options are specific to Proxmox VE and are ignored on all other environments.

| Option | What it does | Default |
|---|---|---|
| `--ctid <id>` | Proxmox container ID number. Must be unique on your Proxmox host. Also used to derive the container's IP address when `--ip` is not specified (the last octet of the IP is set to this value). | `120` |
| `--ip <addr>` | Static IP address for the container (e.g. `192.168.1.150`). If not specified, the script takes your host's IP, replaces the last octet with the CTID value, and uses that. For example, if your host is `192.168.1.10` and CTID is `120`, the container gets `192.168.1.120`. | Auto-derived from host IP + CTID |
| `--gw <addr>` | Default gateway (your router's IP address). The script detects this from the host's routing table. You only need to specify this if auto-detection fails or if the container should use a different gateway. | Auto-detected from host |
| `--cidr <bits>` | Subnet prefix length (e.g. `24` for a `/24` or `255.255.255.0` subnet). Detected from the host's network configuration. You almost never need to set this manually. | Auto-detected from host |
| `--storage <name>` | Proxmox storage pool to create the container's root disk on (e.g. `local-lvm`, `zfs-pool`). If not specified and multiple storage pools are available, the script presents a selection prompt. If only one pool exists, it is used automatically. | Auto (prompt if multiple) |

### NAS environments (Unraid, TrueNAS SCALE)

These options control where Wolf stores its data on NAS systems. Both Unraid and TrueNAS have non-persistent system partitions, so all Wolf data must be stored on persistent storage.

| Option | What it does | Default |
|---|---|---|
| `--appdata <path>` | Full path to the directory where Wolf stores its configuration, Docker Compose file, Steam data, Wolf Den state, and cover art. On Unraid, the convention is `/mnt/user/appdata/wolf`. On TrueNAS, it defaults to `/mnt/<pool>/appdata/wolf` (derived from the selected pool). If you specify `--appdata`, it overrides pool-based derivation on TrueNAS. | `/mnt/user/appdata/wolf` (Unraid), `/mnt/<pool>/appdata/wolf` (TrueNAS) |
| `--pool <name>` | TrueNAS only. Which ZFS pool to store Wolf's appdata on. If not specified and multiple pools exist, the script presents a selection prompt. If only one pool exists, it is used automatically. Ignored if `--appdata` is explicitly set. | Auto (prompt if multiple) |

### NixOS

NixOS is declarative, so the script does not deploy Wolf directly. Instead, it generates a NixOS module file (`wolf.nix`) that you import into your system configuration and apply with `nixos-rebuild switch`. No options are required, but you can control the output:

| Option | What it does | Default |
|---|---|---|
| `--render-node <path>` | Which GPU to use. The selected render node is baked into the generated module. | Auto-detected |

The generated module includes Docker enablement, udev rules, and Wolf + Wolf Den as OCI containers. For NVIDIA GPUs, it also integrates the [wolf-nvidia-vol flake](https://github.com/altano/flakes/tree/main/wolf-nvidia-vol) for automatic driver volume creation, and the script prints the flake input you need to add.

> **Note:** The script does not run `nixos-rebuild`. It generates the configuration and tells you how to apply it. This is intentional: `nixos-rebuild` is a system-wide operation that should be reviewed before running.

### Flag interaction notes

- **`--appdata` and `--pool`**: On TrueNAS, `--pool tank` sets the appdata path to `/mnt/tank/appdata/wolf`. If you also pass `--appdata /some/other/path`, the `--appdata` value wins and `--pool` is ignored.
- **`--ip` and `--ctid`**: On Proxmox, the default container IP is derived from `--ctid`. If you set `--ctid 200` without `--ip`, the container gets IP `x.x.x.200`. Setting `--ip` explicitly overrides this derivation.
- **Cross-environment flags**: Passing a flag that doesn't apply to your environment (e.g. `--ctid` on Docker) is harmless. The flag is parsed and stored, but the environment script never reads it.

### Examples

Run with all defaults (auto-detect everything):

```bash
./wolf.sh
```

Proxmox -- specify a custom container ID and IP:

```bash
./wolf.sh --ctid 150 --ip 192.168.1.150
```

Proxmox -- give the container more resources for demanding games:

```bash
./wolf.sh --cpu 8 --ram 8192 --disk 32
```

Proxmox -- use a specific storage pool:

```bash
./wolf.sh --storage local-zfs
```

LXC / Incus -- give the container a custom name and more resources:

```bash
./wolf.sh --name my-wolf --cpu 8 --ram 8192
```

Unraid -- use a custom appdata path (e.g. cache-only share):

```bash
./wolf.sh --appdata /mnt/cache/appdata/wolf
```

TrueNAS -- specify the pool (skip the selection prompt):

```bash
./wolf.sh --pool tank
```

TrueNAS -- override the default appdata location entirely:

```bash
./wolf.sh --appdata /mnt/tank/custom/wolf-gaming
```

NixOS -- generate the module (auto-detects GPU):

```bash
./wolf.sh
```

NixOS -- specify a GPU for the generated module:

```bash
./wolf.sh --render-node /dev/dri/renderD129
```

Use a specific GPU on any environment (skip the selection prompt):

```bash
./wolf.sh --render-node /dev/dri/renderD129
```

Combine multiple options:

```bash
./wolf.sh --cpu 8 --ram 16384 --disk 64 --render-node /dev/dri/renderD128
```

## What gets created on your system

### Proxmox

- An LXC container (default ID: 120, hostname: `wolf`)
- GPU passthrough configuration in `/etc/pve/lxc/<CTID>.conf`
- Inside the container: Docker, Wolf, and Wolf Den (see Docker section below)

### LXC (standalone)

- An LXC container (default name: `wolf`) in `/var/lib/lxc/wolf/`
- GPU passthrough configuration appended to the container config
- Inside the container: Docker, Wolf, and Wolf Den (see Docker section below)

### Incus

- An Incus container (default name: `wolf`)
- GPU devices added via `incus config device add`
- Inside the container: Docker, Wolf, and Wolf Den (see Docker section below)

### Unraid

| Path | What it is |
|---|---|
| `/mnt/user/appdata/wolf/docker-compose.yml` | Configuration file that tells Docker how to run Wolf + Wolf Den |
| `/mnt/user/appdata/wolf/cfg/config.toml` | Wolf configuration (apps, codec support) |
| `/mnt/user/appdata/wolf/steam/` | Persistent storage for Steam (game installs, saves) |
| `/mnt/user/appdata/wolf/wolf-den/` | Wolf Den state |
| `/mnt/user/appdata/wolf/covers/` | App cover art images |
| `/boot/config/wolf-virtual-inputs.rules` | Udev rules for virtual input (persistent across reboots) |
| `/boot/config/go` | Modified to restore udev rules and auto-start Wolf on boot |

> **Note:** Unraid's root filesystem is a tmpfs (it runs from a USB flash drive), so all persistent data is stored in `/mnt/user/appdata/` and `/boot/config/`. The `--appdata` flag lets you change the appdata location if needed.

### TrueNAS SCALE

| Path | What it is |
|---|---|
| `/mnt/<pool>/appdata/wolf/docker-compose.yml` | Configuration file that tells Docker how to run Wolf + Wolf Den |
| `/mnt/<pool>/appdata/wolf/cfg/config.toml` | Wolf configuration (apps, codec support) |
| `/mnt/<pool>/appdata/wolf/steam/` | Persistent storage for Steam (game installs, saves) |
| `/mnt/<pool>/appdata/wolf/wolf-den/` | Wolf Den state |
| `/mnt/<pool>/appdata/wolf/covers/` | App cover art images |
| `/mnt/<pool>/appdata/wolf/wolf-virtual-inputs.rules` | Udev rules source (on ZFS, survives system updates) |
| `/mnt/<pool>/appdata/wolf/wolf-init.sh` | Boot init script (restores udev rules, starts Wolf) |

> **Note:** TrueNAS SCALE overwrites its system partition on updates, so nothing in `/etc/` persists. All Wolf data lives on a ZFS dataset. The init script is registered with TrueNAS via `midclt` and appears in the TrueNAS UI under System > Advanced > Init/Shutdown Scripts.
>
> **TrueNAS CORE (FreeBSD) is not supported** -- this script requires TrueNAS SCALE, which is Linux-based.

### NixOS

The script does not deploy Wolf directly on NixOS. Instead, it generates a module file:

| Path | What it is |
|---|---|
| `./wolf.nix` | NixOS module defining Docker, udev rules, and Wolf + Wolf Den OCI containers |

After you import `wolf.nix` into your system configuration and run `nixos-rebuild switch`, NixOS manages:

| Managed by NixOS | What it is |
|---|---|
| `virtualisation.docker` | Docker daemon (enabled by the module) |
| `virtualisation.oci-containers` | Wolf and Wolf Den containers (started automatically) |
| `services.udev.extraRules` | Virtual input device rules |
| `/etc/wolf/` | Wolf configuration and data directories (created via `systemd.tmpfiles`) |

For NVIDIA GPUs, the module also references the [wolf-nvidia-vol flake](https://github.com/altano/flakes/tree/main/wolf-nvidia-vol), which builds a driver volume from your system's NVIDIA package. The volume is rebuilt automatically when you update your NVIDIA driver and run `nixos-rebuild`.

### Docker

| Path | What it is |
|---|---|
| `/opt/wolf/docker-compose.yml` | Configuration file that tells Docker how to run Wolf + Wolf Den |
| `/etc/wolf/cfg/config.toml` | Wolf configuration (apps, codec support) |
| `/etc/wolf/steam/` | Persistent storage for Steam (game installs, saves) |
| `/etc/wolf/wolf-den/` | Wolf Den state |
| `/etc/wolf/covers/` | App cover art images |
| `/etc/udev/rules.d/85-wolf-virtual-inputs.rules` | Rules that let Wolf create virtual game controllers |

### Podman

| Path | What it is |
|---|---|
| `/etc/containers/systemd/wolf.container` | Quadlet file for the Wolf streaming service |
| `/etc/containers/systemd/wolf-den.container` | Quadlet file for the Wolf Den web management UI |
| `/etc/wolf/` | Wolf's configuration and data directory |
| `/etc/udev/rules.d/85-wolf-virtual-inputs.rules` | Rules that let Wolf create virtual game controllers |

## GPU support

The script automatically detects your GPU vendor and configures the correct passthrough settings:

| Vendor | Driver | Notes |
|---|---|---|
| **Intel** | `i915` / `xe` | Passthrough via `/dev/dri` only. Works with integrated and Arc GPUs. |
| **AMD** | `amdgpu` | Passthrough via `/dev/dri` + `/dev/kfd` (if available for compute). |
| **NVIDIA** | `nvidia` | Passthrough via `/dev/dri` + NVIDIA device nodes. Requires building a driver volume inside the container (done automatically, takes a few minutes on first run). |

If your server has multiple GPUs, the script lists them with their product names and lets you choose:

```
Available GPUs:
  1) AMD Radeon RX 6900 XT (amdgpu, /dev/dri/renderD128)
  2) Intel UHD Graphics 770 (i915, /dev/dri/renderD129)

Select GPU for Wolf [1]:
```

## Managing Wolf after installation

### Docker

```bash
cd /opt/wolf

# Check if Wolf is running
docker compose ps

# View logs (press Ctrl+C to stop watching)
docker compose logs -f

# Stop Wolf
docker compose stop

# Restart Wolf
docker compose restart

# Update to the latest version
docker compose pull
docker compose up -d
```

### Podman

```bash
# Check if Wolf is running
systemctl status wolf wolf-den

# View logs (press Ctrl+C to stop watching)
journalctl -u wolf -f         # Wolf logs
journalctl -u wolf-den -f     # Wolf Den logs

# Stop Wolf
systemctl stop wolf wolf-den

# Restart Wolf
systemctl restart wolf wolf-den

# Update to the latest version
podman pull ghcr.io/games-on-whales/wolf:stable
podman pull ghcr.io/games-on-whales/wolf-den:stable
systemctl restart wolf wolf-den
```

### Unraid

```bash
cd /mnt/user/appdata/wolf

# Check if Wolf is running
docker compose ps

# View logs (press Ctrl+C to stop watching)
docker compose logs -f

# Stop Wolf
docker compose stop

# Restart Wolf
docker compose restart

# Update to the latest version
docker compose pull
docker compose up -d
```

### TrueNAS SCALE

```bash
# Replace <pool> with your pool name (e.g. tank)
cd /mnt/<pool>/appdata/wolf

# Check if Wolf is running
docker compose ps

# View logs (press Ctrl+C to stop watching)
docker compose logs -f

# Stop Wolf
docker compose stop

# Restart Wolf
docker compose restart

# Update to the latest version
docker compose pull
docker compose up -d
```

### NixOS

Wolf is managed as a NixOS systemd service. Container names are prefixed with `docker-` by NixOS.

```bash
# Check if Wolf is running
systemctl status docker-wolf docker-wolf-den

# View logs (press Ctrl+C to stop watching)
journalctl -u docker-wolf -f       # Wolf logs
journalctl -u docker-wolf-den -f   # Wolf Den logs

# Restart Wolf
systemctl restart docker-wolf docker-wolf-den

# Update to the latest version: edit wolf.nix to change the image tag,
# or pull the latest :stable images and restart
docker pull ghcr.io/games-on-whales/wolf:stable
docker pull ghcr.io/games-on-whales/wolf-den:stable
systemctl restart docker-wolf docker-wolf-den
```

### Proxmox

First enter the container, then use Docker commands:

```bash
# Enter the container (replace 120 with your CTID)
pct enter 120

# Then use the Docker commands listed above
cd /opt/wolf
docker compose ps
docker compose logs -f
# etc.
```

### LXC (standalone)

```bash
# Enter the container
lxc-attach -n wolf

# Then use the Docker commands listed above
cd /opt/wolf
docker compose ps
# etc.
```

### Incus

```bash
# Enter the container
incus exec wolf -- bash

# Then use the Docker commands listed above
cd /opt/wolf
docker compose ps
# etc.
```

## Re-running the script

The script is safe to re-run. It won't break anything if you run it again. On re-runs, already-downloaded helper scripts are reused from disk:

- **Proxmox / LXC / Incus**: If the container already exists, it skips creation and reconfigures GPU passthrough
- **Unraid**: If the compose file exists, it updates and restarts the services. Boot persistence entries in `/boot/config/go` are only added once.
- **TrueNAS SCALE**: If the compose file exists, it updates and restarts the services. The init script registration is updated in place.
- **NixOS**: The script regenerates `wolf.nix`, overwriting the previous version. You still need to run `nixos-rebuild switch` to apply changes.
- **Docker**: If the compose file exists, it updates and restarts the services
- **Podman**: If the Quadlet file exists, it overwrites it with the latest configuration

## Troubleshooting

### "No GPU render devices found"

This means your GPU driver is not loaded. Check with:

```bash
ls /dev/dri/renderD*
```

If nothing shows up, your GPU driver isn't installed or loaded. Search for how to install your GPU driver on your Linux distribution.

### Unraid: "Docker is not available"

Docker must be enabled in the Unraid web UI. Go to **Settings > Docker** and set **Enable Docker** to **Yes**, then click **Apply**. Once Docker is running, re-run the script.

### TrueNAS: "Docker is not available"

Docker support requires TrueNAS SCALE Electric Eel (24.10) or later. Earlier versions of SCALE used Kubernetes (k3s) instead of Docker and are not supported by this script. Check your TrueNAS version in the web UI under **System > General** and upgrade if needed.

### TrueNAS: "midclt not found"

This script only supports **TrueNAS SCALE** (Linux-based). **TrueNAS CORE** is FreeBSD-based and uses jails instead of Docker, which is not compatible with this script. If you're on TrueNAS CORE, consider migrating to TrueNAS SCALE.

### NixOS: Wolf containers not starting after rebuild

Check the container service status:

```bash
systemctl status docker-wolf
journalctl -u docker-wolf -e
```

Common causes: Docker isn't running (`systemctl status docker`), GPU device nodes don't exist (`ls /dev/dri/renderD*`), or the render node in `wolf.nix` doesn't match your GPU (re-run `wolf.sh` to regenerate with the correct node).

### NixOS: NVIDIA volume build fails

Make sure `hardware.nvidia.package` is set in your NixOS configuration and that the NVIDIA driver is working (`nvidia-smi`). The wolf-nvidia-vol flake derives the volume from this package. If you're not using flakes, you'll need to adapt the module to build the NVIDIA volume manually (see the [wolf-nvidia-vol flake docs](https://github.com/altano/flakes/tree/main/wolf-nvidia-vol)).

### "Could not detect environment"

The script couldn't find Proxmox, LXC, Unraid, TrueNAS, NixOS, Podman, or Docker. You need at least one of these installed:

- **Docker**: Follow the [official Docker install guide](https://docs.docker.com/engine/install/)
- **Podman**: Install with your package manager (e.g. `apt install podman` on Debian/Ubuntu)
- **LXC**: Install with `apt install lxc` on Debian/Ubuntu
- **Incus**: Follow the [Incus install guide](https://linuxcontainers.org/incus/docs/main/installing/)
- **Proxmox**: Install from [proxmox.com](https://www.proxmox.com/)

### Moonlight can't find the server

- Make sure Wolf is running (see "Managing Wolf" above)
- Make sure your device (phone/tablet/laptop) is on the **same network** as the server
- Try typing the server's IP address manually in Moonlight instead of waiting for auto-discovery
- Check that your firewall isn't blocking ports 47984-48200

### Games are laggy or stuttering

- Use a **wired ethernet cable** instead of Wi-Fi on the server (Wi-Fi adds lag)
- If on Proxmox, LXC, or Incus, give the container more resources: re-run with `--cpu 8 --ram 8192`
- On the Moonlight client, try lowering the resolution or frame rate in settings
- Make sure no other heavy programs are using your GPU at the same time

### NVIDIA: "Cannot determine NVIDIA driver version"

The NVIDIA driver isn't properly installed. Run:

```bash
nvidia-smi
```

If this command doesn't work or isn't found, you need to install the NVIDIA driver first. Search for "install NVIDIA driver on [your Linux distro]".

## Uninstalling Wolf

### Docker

```bash
cd /opt/wolf
docker compose down
rm -rf /opt/wolf /etc/wolf
rm /etc/udev/rules.d/85-wolf-virtual-inputs.rules
```

### Podman

```bash
systemctl stop wolf wolf-den
systemctl disable wolf wolf-den
rm /etc/containers/systemd/wolf.container /etc/containers/systemd/wolf-den.container
systemctl daemon-reload
podman volume rm wolf-socket
rm -rf /etc/wolf
rm /etc/udev/rules.d/85-wolf-virtual-inputs.rules
```

### Unraid

```bash
cd /mnt/user/appdata/wolf
docker compose down
rm -rf /mnt/user/appdata/wolf
rm /boot/config/wolf-virtual-inputs.rules
rm /etc/udev/rules.d/85-wolf-virtual-inputs.rules
```

Then edit `/boot/config/go` and remove the lines between `# Wolf udev rules` and `# Wolf docker-compose` (inclusive), plus the `docker compose ... up -d` line that follows.

### TrueNAS SCALE

```bash
# Replace <pool> with your pool name
cd /mnt/<pool>/appdata/wolf
docker compose down
rm -rf /mnt/<pool>/appdata/wolf
rm /etc/udev/rules.d/85-wolf-virtual-inputs.rules
```

Then remove the init script from TrueNAS. In the web UI, go to **System > Advanced > Init/Shutdown Scripts** and delete the Wolf entry. Or from the CLI:

```bash
# Find the script ID
midclt call initshutdownscript.query '[["script", "~", "wolf-init.sh"]]'
# Delete it (replace <id> with the ID from the output)
midclt call initshutdownscript.delete <id>
```

### NixOS

Remove the `wolf.nix` import from your `configuration.nix` (or flake), then rebuild:

```bash
sudo nixos-rebuild switch
```

This stops the Wolf containers and removes the udev rules. Optionally, clean up Wolf's data directory:

```bash
sudo rm -rf /etc/wolf
```

If you added the `wolf-nvidia-vol` flake input for NVIDIA, you can also remove it from your `flake.nix`.

### Proxmox

```bash
# Replace 120 with your CTID
pct stop 120
pct destroy 120
```

### LXC (standalone)

```bash
lxc-stop -n wolf
lxc-destroy -n wolf
```

### Incus

```bash
incus stop wolf
incus delete wolf
```

## Network ports

Wolf uses the following network ports. If you have a firewall on your server, you'll need to allow these for Moonlight to connect. On most home networks, no firewall changes are needed.

| Port | Protocol | Purpose |
|---|---|---|
| 47984 | TCP | HTTPS (Moonlight control) |
| 47989 | TCP | HTTP (Moonlight control) |
| 48010 | TCP | RTSP (stream setup) |
| 47998-48000 | UDP | Video stream |
| 48002-48010 | UDP | Audio / input |
| 8080 | TCP | Wolf Den web UI |

To open these ports on a Linux firewall (if needed):

```bash
# Using ufw (Ubuntu/Debian)
sudo ufw allow 47984:48200/tcp
sudo ufw allow 47998:48200/udp
sudo ufw allow 8080/tcp

# Using firewalld (Fedora/CentOS)
sudo firewall-cmd --permanent --add-port=47984-48200/tcp
sudo firewall-cmd --permanent --add-port=47998-48200/udp
sudo firewall-cmd --permanent --add-port=8080/tcp
sudo firewall-cmd --reload
```

## Script architecture

You only need `wolf.sh` to get started. It detects your environment and automatically downloads the helper scripts it needs (requires `curl` or `wget`).

### How it works

1. `wolf.sh` detects your environment (Proxmox, LXC, Incus, Unraid, TrueNAS, Podman, or Docker)
2. It checks whether the required helper scripts (`common.sh` + the environment-specific script) exist in the same directory. If any are missing, it downloads them from GitHub.
3. It sources `common.sh` (shared defaults, argument parser, and helpers) and the environment script.
4. It calls the environment's main function, passing all CLI arguments through. The main function calls `parse_args` (defined in `common.sh`) to populate globals from the command line.

This means `wolf.sh` itself knows nothing about specific flags. All argument definitions live in `common.sh`, and each environment script decides which globals it reads. To add a new flag, you edit `common.sh` (for shared flags) or the environment script (for environment-specific parsing).

### Scripts

| Script | Purpose |
|---|---|
| `wolf.sh` | Entry point: detects environment, downloads helpers, dispatches with all CLI args |
| `common.sh` | Shared defaults, argument parsing (`parse_args`), GPU detection, compose generation, udev rules, NVIDIA volume, LXC GPU config, Wolf config template |
| `proxmox.sh` | Proxmox LXC creation, storage selection, network detection, GPU passthrough |
| `lxc.sh` | Standalone LXC container creation with resource limits |
| `incus.sh` | Incus container creation with device passthrough |
| `unraid.sh` | Unraid deployment with `/boot/config/go` persistence |
| `truenas.sh` | TrueNAS SCALE deployment with ZFS pool selection and `midclt` init scripts |
| `nixos.sh` | NixOS module generation (wolf.nix) for declarative deployment |
| `podman.sh` | Podman Quadlet generation and systemd integration |
| `docker.sh` | Docker Compose deployment |
| `configure.sh` | Container-side setup (pushed into LXC by Proxmox/LXC/Incus scripts, sources `common.sh` independently) |

Helper scripts are downloaded on first run and cached locally for subsequent runs.
