# games-on-whales/wolf

Howling under the Moonlight

> An intelligent wolf is better than a foolish lion.
> 
> &mdash; <cite>Matshona Dhliwayo.</cite>

## Rationale

The goal is to create a backend for [Moonlight](https://moonlight-stream.org/) that can work nicely with a headless remote server that can still leverage HW acceleration where possible.  

It'll be focused on running in Docker so that we can replace [Sunshine](https://github.com/loki-47-6F-64/sunshine) in [GOW](https://github.com/games-on-whales/gow).

### Code structure

I'm trying to separate the Moonlight protocol from the actual implementation so that we can create a portable *C++* library of methods that are **platform agnostic**.

- `include/`: headers and common methods
- `src/`: implementation methods

In order to achieve this we define static methods **with no side effects** that take the required data in input and produce data as output. This way the core of the protocol is fully decoupled from the underlying platform/HTTP server of choice and can be easily re-used by other projects.

___

The platform dependent code will live in `apps/` here is where the HTTP server + Screen recording + HW encoding code will live. The result will be a single binary: `wolf` that will be in charge of running the Moonlight server.

Since the aim of this project is to run on Docker we don't have to support multiple platforms/OS/desktops we'll be focused on implementing a Moonlight backend for `Wayland` and `PipeWire`.

___

Unit tests will live under `tests/`, they'll be focused on testing the library protocol methods. Since this methods are data only it's trivial to setup unit test to verify that the protocol actually works as intended.


## Dependencies

**TODO: this is not a complete list (yet)**

```
cmake libpipewire-0.3-dev libboost-thread-dev libssl-dev
```