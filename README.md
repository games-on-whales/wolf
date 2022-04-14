# games-on-whales/wolf
[![Linux build and test](https://github.com/games-on-whales/wolf/actions/workflows/linux-build-test.yml/badge.svg)](https://github.com/games-on-whales/wolf/actions/workflows/linux-build-test.yml)
[![Discord](https://img.shields.io/discord/856434175455133727.svg?label=&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2)](https://discord.gg/kRGUDHNHt2)
[![GitHub license](https://img.shields.io/github/license/games-on-whales/wolf)](https://github.com/games-on-whales/wolf/blob/main/LICENSE)

Howling under the Moonlight

> An intelligent wolf is better than a foolish lion.
>
> &mdash; <cite>Matshona Dhliwayo.</cite>

## Rationale

The goal is to create an open source backend for [Moonlight](https://moonlight-stream.org/).

In the process we would like to create a **platform agnostic** C++ library that clearly defines the protocol and that
can be used in other projects. While doing so, we are working on documenting and testing the protocol.  
At the start it'll be focused on running in Docker so that we can
replace [Sunshine](https://github.com/loki-47-6F-64/sunshine) in [GOW](https://github.com/games-on-whales/gow) but the
code is already structured in order to support multiple platforms.

## Code structure

The code is written in order to be as readable as possible **with no side effects** and **no global objects/variables**.
We (try to) follow a functional approach where all the methods take immutable inputs and returns new outputs.   
This way the core of the protocol is fully decoupled from the underlying platform/HTTP server of choice and can be
re-used by other projects, also, it enables us to *easily* test most of the methods in isolation.

We are trying to isolate the Moonlight protocol from the actual implementation so that we can create a portable C++
library of stateless methods, where possible, the code has been separated in logical modules.

### src/crypto

The crypto module depends on OpenSSL and provides an interface ([crypto.hpp](src/crypto/crypto/crypto.hpp)) to all the
crypto related methods that are used by the other modules.

### src/moonlight

Here we define the moonlight protocol interface ([protocol.hpp](src/moonlight/moonlight/protocol.hpp)) and the
implementation ([moonlight.cpp](src/moonlight/moonlight.cpp)).  
This module depends on the `crypto` module and [`Boost`](https://www.boost.org/).

### src/wolf

The wolf module defines the glue code in order to make the moonlight protocol to work over an HTTP REST API. Here will
live all the platform dependent code and the result will be an executable binary: `wolf`.  
This module depends on `crypto`, `moonlight`, [`Boost`](https://www.boost.org/), [`FMT`](https://github.com/fmtlib/fmt)
and [`Simple-web-server`](https://gitlab.com/eidheim/Simple-Web-Server/)

### tests

Unit tests will live under `tests/`, they'll be focused on testing the library protocol methods.

## Dependencies

```
cmake g++-10 gcc-10 libpipewire-0.3-dev libboost-thread-dev libboost-filesystem-dev libboost-log-dev libssl-dev
```

## Acknowledgements

- [@loki-47-6F-64](https://github.com/loki-47-6F-64) for creating and
  sharing [Sunshine](https://github.com/loki-47-6F-64/sunshine)
- All the guys at the [Moonlight](https://moonlight-stream.org/) Discord channel, for the tireless help they provide to
  anyone
- [@ReenigneArcher](https://github.com/ReenigneArcher) for beying the first stargazer of the project and taking care of
  keeping [Sunshine alive](https://github.com/SunshineStream/Sunshine)