use smithay::{
    backend::input::KeyState,
    desktop::{PopupKind, Window as WaylandWindow},
    input::{
        keyboard::{KeyboardTarget, KeysymHandle, ModifiersState},
        pointer::{AxisFrame, ButtonEvent, MotionEvent, PointerTarget, RelativeMotionEvent},
        Seat,
    },
    reexports::wayland_server::{backend::ObjectId, protocol::wl_surface::WlSurface},
    utils::{IsAlive, Serial},
    wayland::seat::WaylandFocus,
    xwayland::X11Surface,
};

#[derive(Debug, Clone, PartialEq)]
pub enum FocusTarget {
    Wayland(WaylandWindow),
    X11(X11Surface),
    Popup(PopupKind),
}

impl IsAlive for FocusTarget {
    fn alive(&self) -> bool {
        match self {
            FocusTarget::Wayland(w) => w.alive(),
            FocusTarget::X11(w) => w.alive(),
            FocusTarget::Popup(p) => p.alive(),
        }
    }
}

impl From<super::Window> for FocusTarget {
    fn from(w: super::Window) -> Self {
        match w {
            super::Window::Wayland(w) => FocusTarget::Wayland(w),
            super::Window::X11(s) => FocusTarget::X11(s),
            _ => unreachable!(),
        }
    }
}

impl From<PopupKind> for FocusTarget {
    fn from(p: PopupKind) -> Self {
        FocusTarget::Popup(p)
    }
}

impl KeyboardTarget<super::State> for FocusTarget {
    fn enter(
        &self,
        seat: &Seat<super::State>,
        data: &mut super::State,
        keys: Vec<KeysymHandle<'_>>,
        serial: Serial,
    ) {
        match self {
            FocusTarget::Wayland(w) => KeyboardTarget::enter(w, seat, data, keys, serial),
            FocusTarget::X11(w) => KeyboardTarget::enter(w, seat, data, keys, serial),
            FocusTarget::Popup(p) => {
                KeyboardTarget::enter(p.wl_surface(), seat, data, keys, serial)
            }
        }
    }

    fn leave(&self, seat: &Seat<super::State>, data: &mut super::State, serial: Serial) {
        match self {
            FocusTarget::Wayland(w) => KeyboardTarget::leave(w, seat, data, serial),
            FocusTarget::X11(w) => KeyboardTarget::leave(w, seat, data, serial),
            FocusTarget::Popup(p) => KeyboardTarget::leave(p.wl_surface(), seat, data, serial),
        }
    }

    fn key(
        &self,
        seat: &Seat<super::State>,
        data: &mut super::State,
        key: KeysymHandle<'_>,
        state: KeyState,
        serial: Serial,
        time: u32,
    ) {
        match self {
            FocusTarget::Wayland(w) => w.key(seat, data, key, state, serial, time),
            FocusTarget::X11(w) => w.key(seat, data, key, state, serial, time),
            FocusTarget::Popup(p) => p.wl_surface().key(seat, data, key, state, serial, time),
        }
    }

    fn modifiers(
        &self,
        seat: &Seat<super::State>,
        data: &mut super::State,
        modifiers: ModifiersState,
        serial: Serial,
    ) {
        match self {
            FocusTarget::Wayland(w) => w.modifiers(seat, data, modifiers, serial),
            FocusTarget::X11(w) => w.modifiers(seat, data, modifiers, serial),
            FocusTarget::Popup(p) => p.wl_surface().modifiers(seat, data, modifiers, serial),
        }
    }
}

impl PointerTarget<super::State> for FocusTarget {
    fn enter(&self, seat: &Seat<super::State>, data: &mut super::State, event: &MotionEvent) {
        match self {
            FocusTarget::Wayland(w) => PointerTarget::enter(w, seat, data, event),
            FocusTarget::X11(w) => PointerTarget::enter(w, seat, data, event),
            FocusTarget::Popup(p) => PointerTarget::enter(p.wl_surface(), seat, data, event),
        }
    }

    fn motion(&self, seat: &Seat<super::State>, data: &mut super::State, event: &MotionEvent) {
        match self {
            FocusTarget::Wayland(w) => w.motion(seat, data, event),
            FocusTarget::X11(w) => w.motion(seat, data, event),
            FocusTarget::Popup(p) => p.wl_surface().motion(seat, data, event),
        }
    }

    fn relative_motion(
        &self,
        seat: &Seat<super::State>,
        data: &mut super::State,
        event: &RelativeMotionEvent,
    ) {
        match self {
            FocusTarget::Wayland(w) => w.relative_motion(seat, data, event),
            FocusTarget::X11(w) => w.relative_motion(seat, data, event),
            FocusTarget::Popup(p) => p.wl_surface().relative_motion(seat, data, event),
        }
    }

    fn button(&self, seat: &Seat<super::State>, data: &mut super::State, event: &ButtonEvent) {
        match self {
            FocusTarget::Wayland(w) => w.button(seat, data, event),
            FocusTarget::X11(w) => w.button(seat, data, event),
            FocusTarget::Popup(p) => p.wl_surface().button(seat, data, event),
        }
    }

    fn axis(&self, seat: &Seat<super::State>, data: &mut super::State, frame: AxisFrame) {
        match self {
            FocusTarget::Wayland(w) => w.axis(seat, data, frame),
            FocusTarget::X11(w) => w.axis(seat, data, frame),
            FocusTarget::Popup(p) => p.wl_surface().axis(seat, data, frame),
        }
    }

    fn leave(&self, seat: &Seat<super::State>, data: &mut super::State, serial: Serial, time: u32) {
        match self {
            FocusTarget::Wayland(w) => PointerTarget::leave(w, seat, data, serial, time),
            FocusTarget::X11(w) => PointerTarget::leave(w, seat, data, serial, time),
            FocusTarget::Popup(p) => PointerTarget::leave(p.wl_surface(), seat, data, serial, time),
        }
    }
}

impl WaylandFocus for FocusTarget {
    fn wl_surface(&self) -> Option<WlSurface> {
        match self {
            FocusTarget::Wayland(w) => w.wl_surface(),
            FocusTarget::X11(w) => w.wl_surface(),
            FocusTarget::Popup(p) => Some(p.wl_surface().clone()),
        }
    }

    fn same_client_as(&self, object_id: &ObjectId) -> bool {
        match self {
            FocusTarget::Wayland(w) => w.same_client_as(object_id),
            FocusTarget::X11(w) => w.same_client_as(object_id),
            FocusTarget::Popup(p) => p.wl_surface().same_client_as(object_id),
        }
    }
}
