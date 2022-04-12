#pragma once

#include <pipewire/pipewire.h>
#include <helpers/logger.hpp>

void init() {
  logs::log(logs::debug,
            "Compiled with libpipewire {} - Linked with libpipewire: {}\n",
            pw_get_headers_version(),
            pw_get_library_version());
}