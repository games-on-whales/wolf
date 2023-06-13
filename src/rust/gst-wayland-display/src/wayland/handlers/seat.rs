use smithay::{
    delegate_seat,
    input::{pointer::CursorImageStatus, Seat, SeatHandler, SeatState},
    reexports::wayland_server::Resource,
    wayland::data_device::set_data_device_focus,
};

use crate::comp::{FocusTarget, State};

impl SeatHandler for State {
    type KeyboardFocus = FocusTarget;
    type PointerFocus = FocusTarget;

    fn seat_state(&mut self) -> &mut SeatState<Self> {
        &mut self.seat_state
    }

    fn focus_changed(&mut self, seat: &Seat<Self>, focus: Option<&Self::KeyboardFocus>) {
        if let Some(surface) = focus {
            let client = match surface {
                FocusTarget::Wayland(w) => w.toplevel().wl_surface().client(),
                FocusTarget::Popup(p) => p.wl_surface().client(),
            };
            set_data_device_focus(&self.dh, seat, client);
        } else {
            set_data_device_focus(&self.dh, seat, None);
        }
    }

    fn cursor_image(&mut self, _seat: &Seat<Self>, image: CursorImageStatus) {
        self.cursor_state = image;
    }
}

delegate_seat!(State);
