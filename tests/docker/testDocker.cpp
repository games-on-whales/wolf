#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <catch2/matchers/catch_matchers_contains.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <core/docker.hpp>
#include <docker/json_formatters.hpp>
#include <rfl/toml.hpp>
#include <runners/docker.hpp>
#include <state/config.hpp>
#include <state/serialised_config.hpp>

using Catch::Matchers::Contains;
using Catch::Matchers::Equals;

TEST_CASE("Docker API", "[DOCKER]") {
  docker::init();
  auto docker_socket = utils::get_env("DOCKER_SOCKET_PATH", "/var/run/docker.sock");
  docker::DockerAPI docker_api(docker_socket);

  docker::Container container = {
      .id = "",
      .name = "WolfTestHelloWorld",
      .image = "hello-world",
      .status = docker::CREATED,
      .ports = {docker::Port{.private_port = 1234, .public_port = 1235, .type = docker::TCP}},
      .mounts = {docker::MountPoint{.source = "/tmp/", .destination = "/tmp/", .mode = "ro"}},
      .devices = {docker::Device{.path_on_host = "/dev/input/mice",
                                 .path_in_container = "/dev/input/mice",
                                 .cgroup_permission = "mrw"}},
      .env = {"ASD=true"}};

  auto first_container = docker_api.create(container);
  REQUIRE(first_container.has_value());
  REQUIRE(docker_api.start_by_id(first_container.value().id));
  REQUIRE(docker_api.stop_by_id(first_container.value().id));

  // This should remove the first container and create a new one with the same name
  auto second_container = docker_api.create(first_container.value(), R"({
    "Env": ["AN_ENV_VAR=true"],
    "HostConfig": {
      "IpcMode": "host"
    }
  })");
  REQUIRE(second_container.has_value());
  REQUIRE(first_container->id != second_container->id);
  REQUIRE(first_container->name == second_container->name);

  REQUIRE_THAT(second_container->env, Contains("AN_ENV_VAR=true"));
  REQUIRE_THAT(second_container->env, Contains("ASD=true"));

  REQUIRE(second_container->ports.size() == first_container->ports.size());
  REQUIRE(second_container->devices.size() == first_container->devices.size());
  REQUIRE(second_container->mounts.size() == first_container->mounts.size());

  REQUIRE(!docker_api.remove_by_id(first_container->id)); // This container doesn't exist anymore
  REQUIRE(docker_api.remove_by_id(second_container->id));

  // get_by_id() must return std::nullopt for a removed / non-existent container (not throw): the
  // runner's monitor loop relies on this to stop cleanly instead of dereferencing an empty optional.
  REQUIRE(!docker_api.get_by_id(second_container->id).has_value());
  REQUIRE(!docker_api.get_by_id("0000000000000000000000000000000000000000000000000000000000000000").has_value());

  REQUIRE(docker_api.inspect_image("hello-world").has_value());
  REQUIRE(!docker_api.inspect_image("hello-world:non-existent-tag").has_value());
}

TEST_CASE("Docker TOML", "[DOCKER]") {
  docker::init();
  docker::DockerAPI docker_api;

  auto event_bus = std::make_shared<events::EventBusType>();
  std::string toml_cfg = R"(

    type = "docker"
    name = "WolfTestHelloWorld"
    image = "hello-world"
    mounts = [
      "/tmp/sockets:/tmp/.X11-unix/",
      "/tmp/sockets:/run/user/1000/pulse/:ro"
    ]
    devices = [
      "/dev/input/mice:/dev/input/mice:ro",
      "/a/b/c:/d/e/f",
      "/tmp:/tmp:rw",
    ]
    ports = [
      "1234:1235",
      "1234:1235:udp"
    ]
    env = [
      "LOG_LEVEL=info"
    ]
    base_create_json = "{'HostConfig': {}}"

    )";
  std::istringstream is(toml_cfg, std::ios_base::binary | std::ios_base::in);
  // Round trip: load TOML -> serialize back
  auto runner = state::get_runner(rfl::toml::read<wolf::config::AppDocker>(is).value(), event_bus);
  auto container = rfl::get<wolf::config::AppDocker>(runner->serialize().variant());

  REQUIRE_THAT(container.name, Equals("WolfTestHelloWorld"));
  REQUIRE_THAT(container.image, Equals("hello-world"));

  REQUIRE_THAT(container.ports, Equals(std::vector<std::string>{"1234:1235/tcp", "1234:1235/udp"}));
  REQUIRE_THAT(container.devices,
               Equals(std::vector<std::string>{
                   "/dev/input/mice:/dev/input/mice:ro",
                   "/a/b/c:/d/e/f:mrw",
                   "/tmp:/tmp:rw",
               }));
  REQUIRE_THAT(container.env, Equals(std::vector<std::string>{"LOG_LEVEL=info"}));
  REQUIRE_THAT(container.base_create_json.value(), Equals("{'HostConfig': {}}"));
}

TEST_CASE("Docker create detects container name conflicts", "[DOCKER]") {
  REQUIRE(docker::is_container_name_conflict_response(409, R"({"message":"Conflict"})"));

  REQUIRE(docker::is_container_name_conflict_response(
      500,
      R"({"cause":"that name is already in use",)"
      R"("message":"container create: the container name \"Wolf-UI_123\" is already in use","response":500})"));

  REQUIRE(docker::is_container_name_conflict_response(
      500,
      R"(container create: the container name "Wolf-UI_123" is already in use)"));

  REQUIRE(!docker::is_container_name_conflict_response(500, R"({"message":"storage backend failed"})"));
  REQUIRE(!docker::is_container_name_conflict_response(404, R"({"message":"No such image"})"));
}

TEST_CASE("Parse nulls in json reply", "[DOCKER]") {
  // This is a reply that has been reported in the wild when using Podman
  // Notice the `null` like in the port definition ({"4713/tcp": null})
  auto reply = R""""(
{
  "Id": "774d0082288351f545401417afb5098959c6202e506b3ea3744c75c68bc9f357",
  "Created": "2025-06-18T07:39:53.67642857Z",
  "Path": "/entrypoint.sh",
  "Args": [
    "/entrypoint.sh"
  ],
  "State": {
    "Status": "created",
    "Running": false,
    "Paused": false,
    "Restarting": false,
    "OOMKilled": false,
    "Dead": false,
    "Pid": 0,
    "ExitCode": 0,
    "Error": "",
    "StartedAt": "0001-01-01T00:00:00Z",
    "FinishedAt": "0001-01-01T00:00:00Z",
    "Health": {
      "Status": "",
      "FailingStreak": 0,
      "Log": null
    }
  },
  "Image": "sha256:16867cd26141b14e889ffefa75d9aedfb535718a1e03f9765f1f612c60bda944",
  "ResolvConfPath": "",
  "HostnamePath": "",
  "HostsPath": "",
  "LogPath": "",
  "Name": "/WolfPulseAudio",
  "RestartCount": 0,
  "Driver": "overlay",
  "Platform": "linux",
  "MountLabel": "",
  "ProcessLabel": "",
  "AppArmorProfile": "containers-default-0.50.1",
  "ExecIDs": [],
  "HostConfig": {
    "Binds": [
      "/tmp/sockets:/tmp/pulse/:rw,rprivate,rbind"
    ],
    "ContainerIDFile": "",
    "LogConfig": {
      "Type": "journald",
      "Config": null
    },
    "NetworkMode": "bridge",
    "PortBindings": {
      "4713/tcp": null
    },
    "RestartPolicy": {
      "Name": "",
      "MaximumRetryCount": 0
    },
    "AutoRemove": false,
    "VolumeDriver": "",
    "VolumesFrom": null,
    "CapAdd": [],
    "CapDrop": [
      "AUDIT_WRITE",
      "MKNOD",
      "NET_RAW"
    ],
    "CgroupnsMode": "",
    "Dns": [],
    "DnsOptions": [],
    "DnsSearch": [],
    "ExtraHosts": [],
    "GroupAdd": [],
    "IpcMode": "shareable",
    "Cgroup": "",
    "Links": null,
    "OomScoreAdj": 0,
    "PidMode": "private",
    "Privileged": false,
    "PublishAllPorts": false,
    "ReadonlyRootfs": false,
    "SecurityOpt": [
      "label=disable"
    ],
    "UTSMode": "private",
    "UsernsMode": "",
    "ShmSize": 65536000,
    "Runtime": "oci",
    "ConsoleSize": [
      0,
      0
    ],
    "Isolation": "",
    "CpuShares": 0,
    "Memory": 0,
    "NanoCpus": 0,
    "CgroupParent": "",
    "BlkioWeight": 0,
    "BlkioWeightDevice": null,
    "BlkioDeviceReadBps": null,
    "BlkioDeviceWriteBps": null,
    "BlkioDeviceReadIOps": null,
    "BlkioDeviceWriteIOps": null,
    "CpuPeriod": 0,
    "CpuQuota": 0,
    "CpuRealtimePeriod": 0,
    "CpuRealtimeRuntime": 0,
    "CpusetCpus": "",
    "CpusetMems": "",
    "Devices": null,
    "DeviceCgroupRules": null,
    "DeviceRequests": null,
    "MemoryReservation": 0,
    "MemorySwap": 0,
    "MemorySwappiness": 0,
    "OomKillDisable": false,
    "PidsLimit": 2048,
    "Ulimits": [
      {
        "Name": "RLIMIT_NOFILE",
        "Hard": 1280,
        "Soft": 2560
      },
      {
        "Name": "RLIMIT_NPROC",
        "Hard": 1048576,
        "Soft": 1048576
      }
    ],
    "CpuCount": 0,
    "CpuPercent": 0,
    "IOMaximumIOps": 0,
    "IOMaximumBandwidth": 0,
    "MaskedPaths": null,
    "ReadonlyPaths": null
  },
  "GraphDriver": {
    "Data": {
      "LowerDir": "/data/graphroot/overlay/79b8ed02363b6cd7786573b5743e4dcc0e2495a6db592d283e2112df99d729a7/diff:/data/graphroot/overlay/83883137fe301463563d25c8b493dc4fdaf8ca4e36044299b7ceb1f27c6320fc/diff:/data/graphroot/overlay/1b02221f2209e21c01edd4d73a675514bad307bc8cec0895fc76d2a97687cde9/diff:/data/graphroot/overlay/07a190aac66b86488b03a291a3db257521d5b00c6e8a6a5a5871448e168679a3/diff:/data/graphroot/overlay/e5edd50272bfe60cf41e13d7032b5fd9f8acbdfba24aa0065ee5d40d99e2a773/diff:/data/graphroot/overlay/3c8dc8cff202c4eca4fca7e1bdb203d647119851e50f75b7db9b823a4c1bb6a0/diff:/data/graphroot/overlay/975bb9523045cd9eb661105979a522eab73a0b289c7e68b83bdd7c0a5bbdaeda/diff:/data/graphroot/overlay/816f1a64d8b476d8c76957cb5749d7a1f5d02a2f8d444b901666419f5fc9e1df/diff",
      "UpperDir": "/data/graphroot/overlay/b025276fb1a5c78e3f5f545b3554b3617e718d4b6358a9df0534a41d918d9c19/diff",
      "WorkDir": "/data/graphroot/overlay/b025276fb1a5c78e3f5f545b3554b3617e718d4b6358a9df0534a41d918d9c19/work"
    },
    "Name": "overlay"
  },
  "SizeRootFs": 0,
  "Mounts": [
    {
      "Type": "bind",
      "Source": "/tmp/sockets",
      "Destination": "/tmp/pulse/",
      "Mode": "",
      "RW": true,
      "Propagation": "rprivate"
    }
  ],
  "Config": {
    "Hostname": "774d00822883",
    "Domainname": "",
    "User": "",
    "AttachStdin": false,
    "AttachStdout": false,
    "AttachStderr": false,
    "ExposedPorts": {
      "4713/tcp": {}
    },
    "Tty": false,
    "OpenStdin": false,
    "StdinOnce": false,
    "Env": [
      "TERM=xterm",
      "UMASK=000",
      "HOME=/root",
      "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
      "PGID=1000",
      "XDG_RUNTIME_DIR=/tmp/pulse/",
      "container=podman",
      "TZ=Europe/London",
      "NEEDRESTART_SUSPEND=1",
      "GID=1000",
      "DEBIAN_FRONTEND=noninteractive",
      "PUID=1000",
      "UNAME=retro",
      "UID=1000"
    ],
    "Cmd": [],
    "Image": "ghcr.io/games-on-whales/pulseaudio:master",
    "Volumes": null,
    "WorkingDir": "/",
    "Entrypoint": [
      "/entrypoint.sh"
    ],
    "OnBuild": null,
    "Labels": {
      "org.opencontainers.image.ref.name": "ubuntu",
      "org.opencontainers.image.source": "https://github.com/games-on-whales/gow",
      "org.opencontainers.image.version": "25.04"
    },
    "StopSignal": "15",
    "StopTimeout": 0
  },
  "NetworkSettings": {
    "Bridge": "",
    "SandboxID": "",
    "HairpinMode": false,
    "LinkLocalIPv6Address": "",
    "LinkLocalIPv6PrefixLen": 0,
    "Ports": {
      "4713/tcp": null
    },
    "SandboxKey": "",
    "SecondaryIPAddresses": null,
    "SecondaryIPv6Addresses": null,
    "EndpointID": "",
    "Gateway": "",
    "GlobalIPv6Address": "",
    "GlobalIPv6PrefixLen": 0,
    "IPAddress": "",
    "IPPrefixLen": 0,
    "IPv6Gateway": "",
    "MacAddress": "",
    "Networks": {
      "podman": {
        "IPAMConfig": null,
        "Links": null,
        "Aliases": [
          "774d00822883"
        ],
        "NetworkID": "podman",
        "EndpointID": "",
        "Gateway": "",
        "IPAddress": "",
        "IPPrefixLen": 0,
        "IPv6Gateway": "",
        "GlobalIPv6Address": "",
        "GlobalIPv6PrefixLen": 0,
        "MacAddress": "",
        "DriverOpts": null
      }
    }
  }
}
)"""";

  auto json = utils::parse_json(reply);
  auto parsed_container = boost::json::value_to<docker::Container>(json);

  REQUIRE_THAT(parsed_container.id, Equals("774d0082288351f545401417afb5098959c6202e506b3ea3744c75c68bc9f357"));
  REQUIRE(parsed_container.status == docker::CREATED);

  REQUIRE(parsed_container.mounts.size() == 1);
  REQUIRE_THAT(parsed_container.mounts[0].source, Equals("/tmp/sockets"));
  REQUIRE_THAT(parsed_container.mounts[0].destination, Equals("/tmp/pulse/"));
  REQUIRE_THAT(parsed_container.mounts[0].mode, Equals(""));

  REQUIRE(parsed_container.devices.size() == 0);

  REQUIRE(parsed_container.ports.size() == 1);
  REQUIRE(parsed_container.ports[0].private_port == 4713);
  REQUIRE(parsed_container.ports[0].public_port == 4713);
  REQUIRE(parsed_container.ports[0].type == docker::TCP);

  REQUIRE(parsed_container.env.size() == 14);
  REQUIRE_THAT(parsed_container.env, Contains("XDG_RUNTIME_DIR=/tmp/pulse/"));
}

TEST_CASE("Docker 29.1.5 fail to parse", "[DOCKER]") {

  auto reply = R""""(
{
  "Id": "f77f31b6fe9fadfdb0efe7eec4d576cee82022c6bc21077a663b700e5e3e7475",
  "Created": "2026-01-18T12:41:53.138026437Z",
  "Path": "/start.sh",
  "Args": [],
  "State": {
    "Status": "running",
    "Running": true,
    "Paused": false,
    "Restarting": false,
    "OOMKilled": false,
    "Dead": false,
    "Pid": 11595,
    "ExitCode": 0,
    "Error": "",
    "StartedAt": "2026-01-18T12:42:04.228685634Z",
    "FinishedAt": "0001-01-01T00:00:00Z"
  },
  "Image": "sha256:9f4c90bea645ad49cda9734f48ca847f7fd5558bb3734bcfca8351984b4e78e4",
  "ResolvConfPath": "/var/lib/docker/containers/f77f31b6fe9fadfdb0efe7eec4d576cee82022c6bc21077a663b700e5e3e7475/resolv.conf",
  "HostnamePath": "/var/lib/docker/containers/f77f31b6fe9fadfdb0efe7eec4d576cee82022c6bc21077a663b700e5e3e7475/hostname",
  "HostsPath": "/var/lib/docker/containers/f77f31b6fe9fadfdb0efe7eec4d576cee82022c6bc21077a663b700e5e3e7475/hosts",
  "LogPath": "/var/lib/docker/containers/f77f31b6fe9fadfdb0efe7eec4d576cee82022c6bc21077a663b700e5e3e7475/f77f31b6fe9fadfdb0efe7eec4d576cee82022c6bc21077a663b700e5e3e7475-json.log",
  "Name": "container",
  "RestartCount": 0,
  "Driver": "overlay2",
  "Platform": "linux",
  "MountLabel": "",
  "ProcessLabel": "",
  "AppArmorProfile": "",
  "ExecIDs": null,
  "HostConfig": {
    "Binds": null,
    "ContainerIDFile": "",
    "LogConfig": {
      "Type": "json-file",
      "Config": {}
    },
    "NetworkMode": "asdf_default",
    "PortBindings": {
      "22/tcp": [
        {
          "HostIp": "",
          "HostPort": ""
        }
      ]
    },
    "RestartPolicy": {
      "Name": "unless-stopped",
      "MaximumRetryCount": 0
    },
    "AutoRemove": false,
    "VolumeDriver": "",
    "VolumesFrom": null,
    "ConsoleSize": [
      0,
      0
    ],
    "CapAdd": null,
    "CapDrop": null,
    "CgroupnsMode": "private",
    "Dns": null,
    "DnsOptions": null,
    "DnsSearch": null,
    "ExtraHosts": [],
    "GroupAdd": null,
    "IpcMode": "private",
    "Cgroup": "",
    "Links": null,
    "OomScoreAdj": 0,
    "PidMode": "",
    "Privileged": false,
    "PublishAllPorts": false,
    "ReadonlyRootfs": false,
    "SecurityOpt": null,
    "UTSMode": "",
    "UsernsMode": "",
    "ShmSize": 67108864,
    "Runtime": "runc",
    "Isolation": "",
    "CpuShares": 0,
    "Memory": 0,
    "NanoCpus": 0,
    "CgroupParent": "",
    "BlkioWeight": 0,
    "BlkioWeightDevice": null,
    "BlkioDeviceReadBps": null,
    "BlkioDeviceWriteBps": null,
    "BlkioDeviceReadIOps": null,
    "BlkioDeviceWriteIOps": null,
    "CpuPeriod": 0,
    "CpuQuota": 0,
    "CpuRealtimePeriod": 0,
    "CpuRealtimeRuntime": 0,
    "CpusetCpus": "",
    "CpusetMems": "",
    "Devices": null,
    "DeviceCgroupRules": null,
    "DeviceRequests": null,
    "MemoryReservation": 0,
    "MemorySwap": 0,
    "MemorySwappiness": null,
    "OomKillDisable": null,
    "PidsLimit": null,
    "Ulimits": null,
    "CpuCount": 0,
    "CpuPercent": 0,
    "IOMaximumIOps": 0,
    "IOMaximumBandwidth": 0,
    "MaskedPaths": [
      "/proc/acpi",
      "/proc/asound",
      "/proc/interrupts",
      "/proc/kcore",
      "/proc/keys",
      "/proc/latency_stats",
      "/proc/sched_debug",
      "/proc/scsi",
      "/proc/timer_list",
      "/proc/timer_stats",
      "/sys/devices/virtual/powercap",
      "/sys/firmware"
    ],
    "ReadonlyPaths": [
      "/proc/bus",
      "/proc/fs",
      "/proc/irq",
      "/proc/sys",
      "/proc/sysrq-trigger"
    ]
  },
  "GraphDriver": {
    "Data": {
      "ID": "f77f31b6fe9fadfdb0efe7eec4d576cee82022c6bc21077a663b700e5e3e7475",
      "LowerDir": "/var/lib/docker/overlay2/3899dd27fb7e3a5a995f148694d66eea59f48a1f2c9fb6fcf67107d301b0e253-init/diff:/var/lib/docker/overlay2/fvbc55p9428yvudjlw92aqgex/diff:/var/lib/docker/overlay2/ptm6w2ejvl2z5b8nrcy42xswf/diff:/var/lib/docker/overlay2/8u7yyvdnlt527szmdlhokl0z7/diff:/var/lib/docker/overlay2/jmvvk3wnj3t2nyg3tke36bi5x/diff:/var/lib/docker/overlay2/mn5vq1zuajn6actpei8am02vr/diff:/var/lib/docker/overlay2/bf105daba7f0975f6e43cd8793e0c54bb9387347ab70554baefb09c8d3d9e87f/diff",
      "MergedDir": "/var/lib/docker/overlay2/3899dd27fb7e3a5a995f148694d66eea59f48a1f2c9fb6fcf67107d301b0e253/merged",
      "UpperDir": "/var/lib/docker/overlay2/3899dd27fb7e3a5a995f148694d66eea59f48a1f2c9fb6fcf67107d301b0e253/diff",
      "WorkDir": "/var/lib/docker/overlay2/3899dd27fb7e3a5a995f148694d66eea59f48a1f2c9fb6fcf67107d301b0e253/work"
    },
    "Name": "overlay2"
  },
  "Mounts": [],
  "Config": {
    "Hostname": "f77f31b6fe9f",
    "Domainname": "",
    "User": "",
    "AttachStdin": false,
    "AttachStdout": true,
    "AttachStderr": true,
    "ExposedPorts": {
      "22/tcp": {}
    },
    "Tty": false,
    "OpenStdin": false,
    "StdinOnce": false,
    "Env": [
      "SSH_PASSWORD=root",
      "DOCKER_HOST=tcp://dind:2375",
      "SSH_USER=root",
      "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
      "DEBIAN_FRONTEND=noninteractive"
    ],
    "Cmd": [
      "/start.sh"
    ],
    "Image": "localagi-sshbox",
    "Volumes": null,
    "WorkingDir": "",
    "Entrypoint": null,
    "Labels": {}
  },
  "NetworkSettings": {
    "SandboxID": "10ed32cdb92298a2ed30b65acd0f401feb029a1f2808814f4d96c67703f790a4",
    "SandboxKey": "/var/run/docker/netns/10ed32cdb922",
    "Ports": {
      "22/tcp": [
        {
          "HostIp": "0.0.0.0",
          "HostPort": "32769"
        },
        {
          "HostIp": "::",
          "HostPort": "32769"
        }
      ]
    },
    "Networks": {
      "localagi_default": {
        "IPAMConfig": null,
        "Links": null,
        "Aliases": [
          "localagi-sshbox-1",
          "sshbox"
        ],
        "DriverOpts": null,
        "GwPriority": 0,
        "NetworkID": "25f4b3255fc26ee84d7bcda5d9bff57e44bf51546272a4d1701f76d0d9e06116",
        "EndpointID": "541f2b1a12fde7b709c9f4d52e79dedfed49cd7ea712243ba59d1725fc60d92d",
        "Gateway": "172.20.0.1",
        "IPAddress": "172.20.0.5",
        "MacAddress": "ca:55:c8:2d:45:6e",
        "IPPrefixLen": 16,
        "IPv6Gateway": "",
        "GlobalIPv6Address": "",
        "GlobalIPv6PrefixLen": 0,
        "DNSNames": [
          "localagi-sshbox-1",
          "sshbox",
          "f77f31b6fe9f"
        ]
      }
    }
  }
})"""";
  auto json = utils::parse_json(reply);
  auto parsed_container = boost::json::value_to<docker::Container>(json);

  REQUIRE_THAT(parsed_container.id, Equals("f77f31b6fe9fadfdb0efe7eec4d576cee82022c6bc21077a663b700e5e3e7475"));
  REQUIRE(parsed_container.ports.size() == 1);
  REQUIRE(parsed_container.ports[0].private_port == -1); // This was causing issues because we get an empty string now
  REQUIRE(parsed_container.ports[0].public_port == 22);
  REQUIRE(parsed_container.ports[0].type == docker::TCP);
}
