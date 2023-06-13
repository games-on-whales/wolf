use smithay::{
    delegate_xdg_shell,
    desktop::{
        find_popup_root_surface, PopupKeyboardGrab, PopupKind, PopupPointerGrab,
        PopupUngrabStrategy, Window,
    },
    input::{pointer::Focus, Seat},
    reexports::wayland_server::protocol::wl_seat::WlSeat,
    utils::Serial,
    wayland::{
        seat::WaylandFocus,
        shell::xdg::{
            PopupSurface, PositionerState, ToplevelSurface, XdgShellHandler, XdgShellState,
        },
    },
};

use crate::comp::{FocusTarget, State};

impl XdgShellHandler for State {
    fn xdg_shell_state(&mut self) -> &mut XdgShellState {
        &mut self.shell_state
    }

    fn new_toplevel(&mut self, surface: ToplevelSurface) {
        let window = Window::new(surface);
        self.pending_windows.push(window);
    }

    fn new_popup(&mut self, surface: PopupSurface, positioner: PositionerState) {
        // TODO: properly recompute the geometry with the whole of positioner state
        surface.with_pending_state(|state| {
            // NOTE: This is not really necessary as the default geometry
            // is already set the same way, but for demonstrating how
            // to set the initial popup geometry this code is left as
            // an example
            state.geometry = positioner.get_geometry();
        });
        if let Err(err) = self.popups.track_popup(PopupKind::from(surface)) {
            tracing::warn!(?err, "Failed to track popup.");
        }
    }

    fn grab(&mut self, surface: PopupSurface, seat: WlSeat, serial: Serial) {
        let seat: Seat<State> = Seat::from_resource(&seat).unwrap();
        let kind = PopupKind::Xdg(surface.clone());
        if let Some(root) = find_popup_root_surface(&kind).ok().and_then(|root| {
            self.space
                .elements()
                .find(|w| w.wl_surface().map(|s| s == root).unwrap_or(false))
                .cloned()
                .map(FocusTarget::from)
        }) {
            let ret = self.popups.grab_popup(root, surface.into(), &seat, serial);
            if let Ok(mut grab) = ret {
                if let Some(keyboard) = seat.get_keyboard() {
                    if keyboard.is_grabbed()
                        && !(keyboard.has_grab(serial)
                            || keyboard.has_grab(grab.previous_serial().unwrap_or(serial)))
                    {
                        grab.ungrab(PopupUngrabStrategy::All);
                        return;
                    }
                    keyboard.set_focus(self, grab.current_grab(), serial);
                    keyboard.set_grab(PopupKeyboardGrab::new(&grab), serial);
                }
                if let Some(pointer) = seat.get_pointer() {
                    if pointer.is_grabbed()
                        && !(pointer.has_grab(serial)
                            || pointer
                                .has_grab(grab.previous_serial().unwrap_or_else(|| grab.serial())))
                    {
                        grab.ungrab(PopupUngrabStrategy::All);
                        return;
                    }
                    pointer.set_grab(self, PopupPointerGrab::new(&grab), serial, Focus::Clear);
                }
            }
        }
    }
}

delegate_xdg_shell!(State);
