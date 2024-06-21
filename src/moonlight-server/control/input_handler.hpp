#pragma once

#include <control/control.hpp>
#include <moonlight/control.hpp>
#include <state/data-structures.hpp>

namespace control {

using namespace moonlight::control::pkts;

/**
 * Side effect: session devices might be updated when hotplugging
 */
void handle_input(state::StreamSession &session,
                  const immer::atom<enet_clients_map> &connected_clients,
                  INPUT_PKT *pkt);

void mouse_move_rel(const MOUSE_MOVE_REL_PACKET &pkt, state::StreamSession &session);

void mouse_move_abs(const MOUSE_MOVE_ABS_PACKET &pkt, state::StreamSession &session);

void mouse_button(const MOUSE_BUTTON_PACKET &pkt, state::StreamSession &session);

void mouse_scroll(const MOUSE_SCROLL_PACKET &pkt, state::StreamSession &session);

void mouse_h_scroll(const MOUSE_HSCROLL_PACKET &pkt, state::StreamSession &session);

void keyboard_key(const KEYBOARD_PACKET &pkt, state::StreamSession &session);

void utf8_text(const UTF8_TEXT_PACKET &pkt, state::StreamSession &session);

void touch(const TOUCH_PACKET &pkt, state::StreamSession &session);

void pen(const PEN_PACKET &pkt, state::StreamSession &session);

void controller_arrival(const CONTROLLER_ARRIVAL_PACKET &pkt,
                        state::StreamSession &session,
                        const immer::atom<enet_clients_map> &connected_clients);

void controller_multi(const CONTROLLER_MULTI_PACKET &pkt,
                      state::StreamSession &session,
                      const immer::atom<enet_clients_map> &connected_clients);

void controller_touch(const CONTROLLER_TOUCH_PACKET &pkt, state::StreamSession &session);

void controller_motion(const CONTROLLER_MOTION_PACKET &pkt, state::StreamSession &session);

void controller_battery(const CONTROLLER_BATTERY_PACKET &pkt, state::StreamSession &session);

} // namespace control
