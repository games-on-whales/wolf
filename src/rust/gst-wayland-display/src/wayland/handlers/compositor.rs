use smithay::{
    backend::renderer::utils::{import_surface_tree, on_commit_buffer_handler},
    delegate_compositor,
    desktop::PopupKind,
    reexports::{
        wayland_protocols::xdg::shell::server::xdg_toplevel::State as XdgState,
        wayland_server::protocol::{wl_buffer::WlBuffer, wl_surface::WlSurface},
    },
    utils::{Size, SERIAL_COUNTER},
    wayland::{
        buffer::BufferHandler,
        compositor::{with_states, CompositorHandler, CompositorState},
        seat::WaylandFocus,
        shell::xdg::{XdgPopupSurfaceData, XdgToplevelSurfaceData},
    },
};

use crate::comp::{FocusTarget, State};

impl BufferHandler for State {
    fn buffer_destroyed(&mut self, _buffer: &WlBuffer) {}
}

impl CompositorHandler for State {
    fn compositor_state(&mut self) -> &mut CompositorState {
        &mut self.compositor_state
    }

    fn commit(&mut self, surface: &WlSurface) {
        on_commit_buffer_handler(surface);

        if let Err(err) = import_surface_tree(&mut self.renderer, surface) {
            tracing::warn!(?err, "Failed to load client buffer.");
        }

        if let Some(window) = self
            .space
            .elements()
            .find(|w| w.wl_surface().as_ref() == Some(surface))
        {
            window.on_commit();
        }
        self.popups.commit(surface);

        // send the initial configure if relevant
        if let Some(idx) = self
            .pending_windows
            .iter_mut()
            .position(|w| w.wl_surface().as_ref() == Some(surface))
        {
            let window = self.pending_windows.swap_remove(idx);

            let toplevel = window.toplevel();
            let (initial_configure_sent, max_size) = with_states(surface, |states| {
                let attributes = states.data_map.get::<XdgToplevelSurfaceData>().unwrap();
                let attributes_guard = attributes.lock().unwrap();

                (
                    attributes_guard.initial_configure_sent,
                    attributes_guard.max_size,
                )
            });

            if self.output.is_none() {
                return;
            }

            if !initial_configure_sent {
                if max_size.w == 0 && max_size.h == 0 {
                    toplevel.with_pending_state(|state| {
                        state.size = Some(
                            self.output
                                .as_ref()
                                .unwrap()
                                .current_mode()
                                .unwrap()
                                .size
                                .to_f64()
                                .to_logical(
                                    self.output
                                        .as_ref()
                                        .unwrap()
                                        .current_scale()
                                        .fractional_scale(),
                                )
                                .to_i32_round(),
                        );
                        state.states.set(XdgState::Fullscreen);
                    });
                }
                toplevel.with_pending_state(|state| {
                    state.states.set(XdgState::Activated);
                });
                toplevel.send_configure();
                self.pending_windows.push(window);
            } else {
                let window_size = toplevel.current_state().size.unwrap_or((0, 0).into());
                let output_size: Size<i32, _> = self
                    .output
                    .as_ref()
                    .unwrap()
                    .current_mode()
                    .unwrap()
                    .size
                    .to_f64()
                    .to_logical(
                        self.output
                            .as_ref()
                            .unwrap()
                            .current_scale()
                            .fractional_scale(),
                    )
                    .to_i32_round();
                let loc = (
                    (output_size.w / 2) - (window_size.w / 2),
                    (output_size.h / 2) - (window_size.h / 2),
                );
                self.space.map_element(window.clone(), loc, true);
                self.seat.get_keyboard().unwrap().set_focus(
                    self,
                    Some(FocusTarget::from(window)),
                    SERIAL_COUNTER.next_serial(),
                );
            }

            return;
        }

        if let Some(popup) = self.popups.find_popup(surface) {
            let PopupKind::Xdg(ref popup) = popup;
            let initial_configure_sent = with_states(surface, |states| {
                states
                    .data_map
                    .get::<XdgPopupSurfaceData>()
                    .unwrap()
                    .lock()
                    .unwrap()
                    .initial_configure_sent
            });
            if !initial_configure_sent {
                // NOTE: This should never fail as the initial configure is always
                // allowed.
                popup.send_configure().expect("initial configure failed");
            }

            return;
        };
    }
}

delegate_compositor!(State);
