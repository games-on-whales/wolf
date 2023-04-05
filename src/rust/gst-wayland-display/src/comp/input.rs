use crate::wayland::protocols::pointer_constraints::{with_pointer_constraint, PointerConstraint};

use super::{focus::FocusTarget, State};
use smithay::{
    backend::{
        input::{
            Axis, AxisSource, Event, InputEvent, KeyState, KeyboardKeyEvent, PointerAxisEvent,
            PointerButtonEvent, PointerMotionEvent,
        },
        libinput::LibinputInputBackend,
    },
    input::{
        keyboard::{keysyms, FilterResult},
        pointer::{AxisFrame, ButtonEvent, MotionEvent, RelativeMotionEvent},
    },
    reexports::{
        input::LibinputInterface,
        nix::{fcntl, fcntl::OFlag, sys::stat},
        wayland_server::protocol::wl_pointer,
    },
    utils::{Logical, Point, Serial, SERIAL_COUNTER, Rectangle},
};
use std::{
    os::{fd::FromRawFd, unix::io::OwnedFd},
    path::Path,
    time::Instant,
};

pub struct NixInterface;

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
                    |data, modifiers, handle| {
                        if state == KeyState::Pressed {
                            if modifiers.ctrl
                                && modifiers.shift
                                && !modifiers.alt
                                && !modifiers.logo
                            {
                                match handle.modified_sym() {
                                    keysyms::KEY_Tab => {
                                        if let Some(element) = data.space.elements().last().cloned()
                                        {
                                            data.surpressed_keys.insert(keysyms::KEY_Tab);
                                            let location =
                                                data.space.element_location(&element).unwrap();
                                            data.space.map_element(element.clone(), location, true);
                                            data.seat.get_keyboard().unwrap().set_focus(
                                                data,
                                                Some(FocusTarget::from(element)),
                                                serial,
                                            );
                                            return FilterResult::Intercept(());
                                        }
                                    }
                                    keysyms::KEY_Q => {
                                        if let Some(target) =
                                            data.seat.get_keyboard().unwrap().current_focus()
                                        {
                                            match target {
                                                FocusTarget::Wayland(window) => {
                                                    window.toplevel().send_close();
                                                }
                                                _ => return FilterResult::Forward,
                                            };
                                            data.surpressed_keys.insert(keysyms::KEY_Q);
                                            return FilterResult::Intercept(());
                                        }
                                    }
                                    _ => {}
                                }
                            }
                        } else {
                            if data.surpressed_keys.remove(&handle.modified_sym()) {
                                return FilterResult::Intercept(());
                            }
                        }

                        FilterResult::Forward
                    },
                );
            }
            InputEvent::PointerMotion { event, .. } => {
                self.last_pointer_movement = Instant::now();
                let serial = SERIAL_COUNTER.next_serial();
                let delta = event.delta();
                let pointer = self.seat.get_pointer().unwrap();

                if let Some((window, pos)) = self
                    .space
                    .element_under(self.pointer_location)
                {
                    let window = window.clone();
                    let surface = window.toplevel().wl_surface().clone();
                    with_pointer_constraint(&surface, |mut constraint| match constraint.as_deref_mut() {
                        Some(PointerConstraint::Locked(locked)) => {
                            if !locked.is_active() {
                                let pending_pointer_location = self.pointer_location + delta;
                                let window_location = self.space.element_location(&window).unwrap();
                            
                                if locked.region.as_ref().map(|region| region.contains(pending_pointer_location.to_i32_round() - window_location)).unwrap_or(true) {
                                    locked.activate();
                                }
                            }
                            if !locked.is_active() {
                                self.pointer_location += delta;
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
                            }
                        },
                        Some(PointerConstraint::Confined(confined)) => {
                            let pending_pointer_location = self.pointer_location + delta;
                            let window_location = self.space.element_location(&window).unwrap();
                            let is_in_confined_region = confined.region.as_ref().map(|region| region.contains(pending_pointer_location.to_i32_round() - window_location)).unwrap_or(true);
                            if !confined.is_active() && is_in_confined_region {
                                confined.activate();
                            }
                            if is_in_confined_region {
                                self.pointer_location = pending_pointer_location;
                                pointer.motion(
                                    self,
                                    Some((window.clone().into(), pos)),
                                    &MotionEvent {
                                        location: self.pointer_location,
                                        serial,
                                        time: event.time_msec(),
                                    },
                                );
                            }
                        },
                        None => {
                            self.pointer_location += delta;
                            if let Some(output) = self.output.as_ref() {
                                if let Some(mode) = output.current_mode() {
                                    self.pointer_location = self.clamp_coords(self.pointer_location, Rectangle::from_loc_and_size((0.0, 0.0), mode.size.to_f64().to_logical(1.0)));
                                }
                            }
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
                        }
                    });

                    let under = self
                        .space
                        .element_under(self.pointer_location)
                        .map(|(w, pos)| (w.clone().into(), pos));
                             
                    pointer.relative_motion(
                        self,
                        under.map(|(w, pos)| (FocusTarget::Wayland(w), pos)),
                        &RelativeMotionEvent {
                            delta,
                            delta_unaccel: event.delta_unaccel(),
                            utime: event.time(),
                        },
                    );
                }
            }
            InputEvent::PointerMotionAbsolute { event } => {
                self.last_pointer_movement = Instant::now();
                let serial = SERIAL_COUNTER.next_serial();
                if let Some(output) = self.output.as_ref() {
                    let output_size = output
                        .current_mode()
                        .unwrap()
                        .size
                        .to_f64()
                        .to_logical(output.current_scale().fractional_scale())
                        .to_i32_round();
                    self.pointer_location = (
                        event.absolute_x_transformed(output_size.w),
                        event.absolute_y_transformed(output_size.h),
                    )
                        .into();

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
                }
            }
            InputEvent::PointerButton { event, .. } => {
                self.last_pointer_movement = Instant::now();
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
                self.last_pointer_movement = Instant::now();
                let horizontal_amount = event
                    .amount(Axis::Horizontal)
                    .or_else(|| event.amount_discrete(Axis::Horizontal).map(|x| x * 2.0))
                    .unwrap_or(0.0);
                let vertical_amount = event
                    .amount(Axis::Vertical)
                    .or_else(|| event.amount_discrete(Axis::Vertical).map(|y| y * 2.0))
                    .unwrap_or(0.0);
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

    fn clamp_coords(&self, pos: Point<f64, Logical>, region: Rectangle<f64, Logical>) -> Point<f64, Logical> {
        (
            pos.x.max(region.loc.x).min(region.size.w as f64),
            pos.y.max(region.loc.y).min(region.size.h as f64),
        ).into()
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
                keyboard.set_focus(self, Some(FocusTarget::from(window)), serial);
                return;
            }
        }
    }
}
