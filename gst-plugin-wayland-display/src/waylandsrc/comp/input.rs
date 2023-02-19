use super::{focus::FocusTarget, State};
use smithay::{
    backend::{
        input::{
            Axis, AxisSource, Event, InputEvent, KeyboardKeyEvent, PointerAxisEvent,
            PointerButtonEvent, PointerMotionEvent,
        },
        libinput::LibinputInputBackend,
    },
    input::{
        keyboard::FilterResult,
        pointer::{AxisFrame, ButtonEvent, MotionEvent, RelativeMotionEvent},
    },
    reexports::{
        input::LibinputInterface,
        nix::{fcntl, fcntl::OFlag, sys::stat},
        wayland_server::protocol::wl_pointer,
    },
    utils::{Logical, Point, Serial, SERIAL_COUNTER},
};
use std::{
    os::{fd::FromRawFd, unix::io::OwnedFd},
    path::Path,
};

pub struct NixInterface;

impl NixInterface {
    pub fn new(_log: impl Into<slog::Logger>) -> NixInterface {
        NixInterface
    }
}

impl LibinputInterface for NixInterface {
    fn open_restricted(&mut self, path: &Path, flags: i32) -> Result<OwnedFd, i32> {
        fcntl::open(path, OFlag::from_bits_truncate(flags), stat::Mode::empty())
            .map(|fd| unsafe { OwnedFd::from_raw_fd(fd) })
            .map_err(|err| err as i32)
    }
    fn close_restricted(&mut self, fd: OwnedFd) {
        let _ = fd;
    }
}

impl State {
    pub fn process_input_event(&mut self, event: InputEvent<LibinputInputBackend>) {
        match event {
            InputEvent::Keyboard { event, .. } => {
                let keycode = event.key_code();
                let state = event.state();
                let serial = SERIAL_COUNTER.next_serial();
                let time = event.time_msec();
                let keyboard = self.seat.get_keyboard().unwrap();

                keyboard.input::<(), _>(
                    self,
                    keycode,
                    state,
                    serial,
                    time,
                    |_data, _modifiers, _handle| FilterResult::Forward,
                );
            }
            InputEvent::PointerMotion { event, .. } => {
                let serial = SERIAL_COUNTER.next_serial();
                let delta = event.delta();
                self.pointer_location += delta;
                self.pointer_location = self.clamp_coords(self.pointer_location);

                let pointer = self.seat.get_pointer().unwrap();
                let under = self
                    .space
                    .element_under(self.pointer_location)
                    .map(|(w, pos)| (w.clone().into(), pos));
                pointer.motion(
                    self,
                    under.clone(),
                    &MotionEvent {
                        location: self.pointer_location,
                        serial,
                        time: event.time_msec(),
                    },
                );
                pointer.relative_motion(
                    self,
                    under,
                    &RelativeMotionEvent {
                        delta,
                        delta_unaccel: event.delta_unaccel(),
                        utime: event.time(),
                    },
                )
            }
            InputEvent::PointerButton { event, .. } => {
                let serial = SERIAL_COUNTER.next_serial();
                let button = event.button_code();

                let state = wl_pointer::ButtonState::from(event.state());
                if wl_pointer::ButtonState::Pressed == state {
                    self.update_keyboard_focus(serial);
                };
                self.seat.get_pointer().unwrap().button(
                    self,
                    &ButtonEvent {
                        button,
                        state: state.try_into().unwrap(),
                        serial,
                        time: event.time_msec(),
                    },
                );
            }
            InputEvent::PointerAxis { event, .. } => {
                let horizontal_amount = event
                    .amount(Axis::Horizontal)
                    .unwrap_or_else(|| event.amount_discrete(Axis::Horizontal).unwrap() * 2.0);
                let vertical_amount = event
                    .amount(Axis::Vertical)
                    .unwrap_or_else(|| event.amount_discrete(Axis::Vertical).unwrap() * 2.0);
                let horizontal_amount_discrete = event.amount_discrete(Axis::Horizontal);
                let vertical_amount_discrete = event.amount_discrete(Axis::Vertical);

                {
                    let mut frame = AxisFrame::new(event.time_msec()).source(event.source());
                    if horizontal_amount != 0.0 {
                        frame = frame.value(Axis::Horizontal, horizontal_amount);
                        if let Some(discrete) = horizontal_amount_discrete {
                            frame = frame.discrete(Axis::Horizontal, discrete as i32);
                        }
                    } else if event.source() == AxisSource::Finger {
                        frame = frame.stop(Axis::Horizontal);
                    }
                    if vertical_amount != 0.0 {
                        frame = frame.value(Axis::Vertical, vertical_amount);
                        if let Some(discrete) = vertical_amount_discrete {
                            frame = frame.discrete(Axis::Vertical, discrete as i32);
                        }
                    } else if event.source() == AxisSource::Finger {
                        frame = frame.stop(Axis::Vertical);
                    }
                    self.seat.get_pointer().unwrap().axis(self, frame);
                }
            }
            _ => {}
        }
    }

    fn clamp_coords(&self, pos: Point<f64, Logical>) -> Point<f64, Logical> {
        if let Some(output) = self.output.as_ref() {
            if let Some(mode) = output.current_mode() {
                return (
                    pos.x.max(0.0).min(mode.size.w as f64),
                    pos.y.max(0.0).min(mode.size.h as f64),
                )
                    .into();
            }
        }
        pos
    }

    fn update_keyboard_focus(&mut self, serial: Serial) {
        let pointer = self.seat.get_pointer().unwrap();
        let keyboard = self.seat.get_keyboard().unwrap();
        // change the keyboard focus unless the pointer or keyboard is grabbed
        // We test for any matching surface type here but always use the root
        // (in case of a window the toplevel) surface for the focus.
        // So for example if a user clicks on a subsurface or popup the toplevel
        // will receive the keyboard focus. Directly assigning the focus to the
        // matching surface leads to issues with clients dismissing popups and
        // subsurface menus (for example firefox-wayland).
        // see here for a discussion about that issue:
        // https://gitlab.freedesktop.org/wayland/wayland/-/issues/294
        if !pointer.is_grabbed() && !keyboard.is_grabbed() {
            if let Some((window, _)) = self
                .space
                .element_under(self.pointer_location)
                .map(|(w, p)| (w.clone(), p))
            {
                self.space.raise_element(&window, true);
                if let super::window::Window::X11(ref surface) = window {
                    let _ = self.xwm.as_mut().unwrap().raise_window(surface);
                }
                keyboard.set_focus(self, Some(FocusTarget::from(window)), serial);
                return;
            }
        }
    }
}
