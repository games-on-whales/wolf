#pragma once

#include <events/events.hpp>
#include <state/data-structures.hpp>

namespace wolf::api {

void start_server(immer::box<state::AppState> app_state);

} // namespace wolf::api