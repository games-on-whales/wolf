use smithay::{
    utils::{Logical, Rectangle, SERIAL_COUNTER},
    xwayland::{
        xwm::{Reorder, WmWindowType, XwmId},
        X11Surface, X11Wm, XwmHandler,
    },
};

use crate::comp::{Data, FocusTarget, Window};

impl XwmHandler for Data {
    fn xwm_state(&mut self, _: XwmId) -> &mut X11Wm {
        self.state.xwm.as_mut().unwrap()
    }

    fn new_window(&mut self, _: XwmId, _window: X11Surface) {}
    fn new_override_redirect_window(&mut self, _: XwmId, _window: X11Surface) {}

    fn map_window_request(&mut self, id: XwmId, window: X11Surface) {
        if !matches!(
            window.window_type(),
            None | Some(WmWindowType::Normal)
                | Some(WmWindowType::Utility)
                | Some(WmWindowType::Splash)
        ) {
            let geo = window.geometry();
            let _ = window.set_mapped(true);
            let _ = window.set_activated(true);
            let _ = window.configure(geo);
            let _ = self.xwm_state(id).raise_window(&window);
            let window = Window::X11(window);
            self.state.space.map_element(window.clone(), geo.loc, true);
            self.state.seat.get_keyboard().unwrap().set_focus(
                &mut self.state,
                Some(FocusTarget::from(window)),
                SERIAL_COUNTER.next_serial(),
            );
            return;
        }

        let output_geo = if let Some(output) = self.state.output.as_ref() {
            Rectangle::from_loc_and_size(
                (0, 0),
                output
                    .current_mode()
                    .unwrap()
                    .size
                    .to_f64()
                    .to_logical(output.current_scale().fractional_scale())
                    .to_i32_round(),
            )
        } else {
            Rectangle::from_loc_and_size((0, 0), (800, 600))
        };

        let window_size = if window.window_type() == Some(WmWindowType::Splash) {
            // don't resize splashes
            window.geometry().size
        } else {
            // if max_size doesn't prohibit it, give it the full output by default
            window
                .max_size()
                .map(|size| Rectangle::from_loc_and_size((0, 0), size))
                .unwrap_or(output_geo)
                .intersection(output_geo)
                .unwrap()
                .size
        };
        // center it
        let window_loc = (
            (output_geo.size.w / 2) - (window_size.w / 2),
            (output_geo.size.h / 2) - (window_size.h / 2),
        );

        let _ = window.set_mapped(true);
        if window.window_type() != Some(WmWindowType::Splash) {
            let _ = window.set_fullscreen(true);
        }
        let _ = window.set_activated(true);
        let _ = window.configure(Rectangle::from_loc_and_size(window_loc, window_size));
        let _ = self.xwm_state(id).raise_window(&window);

        let window = Window::X11(window);
        self.state
            .space
            .map_element(window.clone(), window_loc, true);
        self.state.seat.get_keyboard().unwrap().set_focus(
            &mut self.state,
            Some(FocusTarget::from(window)),
            SERIAL_COUNTER.next_serial(),
        );
    }

    fn mapped_override_redirect_window(&mut self, _: XwmId, window: X11Surface) {
        let geo = window.geometry();
        let window = Window::from(window);
        self.state.space.map_element(window.clone(), geo.loc, true);
    }

    fn unmapped_window(&mut self, _: XwmId, window: X11Surface) {
        let maybe = self
            .state
            .space
            .elements()
            .find(|e| matches!(e, Window::X11(w) if w == &window))
            .cloned();
        if let Some(elem) = maybe {
            self.state.space.unmap_elem(&elem)
        }
        if !window.is_override_redirect() {
            window.set_mapped(false).unwrap();
        }
    }

    fn destroyed_window(&mut self, _: XwmId, _window: X11Surface) {}

    fn configure_request(
        &mut self,
        _: XwmId,
        window: X11Surface,
        x: Option<i32>,
        y: Option<i32>,
        w: Option<u32>,
        h: Option<u32>,
        _reorder: Option<Reorder>,
    ) {
        let mut geo = window.geometry();
        if !self
            .state
            .space
            .elements()
            .find(|e| matches!(e, Window::X11(w) if w == &window))
            .is_some()
        {
            // The window is not yet mapped, lets respect the initial position
            if let Some(x) = x {
                geo.loc.x = x;
            }
            if let Some(y) = y {
                geo.loc.y = y;
            }
        }

        if let Some(w) = w {
            geo.size.w = w as i32;
        }
        if let Some(h) = h {
            geo.size.h = h as i32;
        }
        let _ = window.configure(geo);
    }

    fn configure_notify(
        &mut self,
        _: XwmId,
        window: X11Surface,
        geometry: Rectangle<i32, Logical>,
        _above: Option<u32>,
    ) {
        if window.is_override_redirect() {
            let Some(elem) = self
                .state
                .space
                .elements()
                .find(|e| matches!(e, Window::X11(w) if w == &window))
                .cloned()
            else { return };
            self.state.space.map_element(elem, geometry.loc, false);
        }
    }

    fn resize_request(
        &mut self,
        _: XwmId,
        _window: X11Surface,
        _button: u32,
        _resize_edge: smithay::xwayland::xwm::ResizeEdge,
    ) {
    }
    fn move_request(&mut self, _: XwmId, _window: X11Surface, _button: u32) {}

    fn fullscreen_request(&mut self, id: XwmId, window: X11Surface) {
        if self.state.output.is_none() {
            return;
        }

        let maybe = self
            .state
            .space
            .elements()
            .find(|e| matches!(e, Window::X11(w) if w == &window))
            .cloned();
        if let Some(elem) = maybe {
            let _ = window.set_fullscreen(true);

            let output_geo = Rectangle::from_loc_and_size(
                (0, 0),
                self.state
                    .output
                    .as_ref()
                    .unwrap()
                    .current_mode()
                    .unwrap()
                    .size
                    .to_f64()
                    .to_logical(
                        self.state
                            .output
                            .as_ref()
                            .unwrap()
                            .current_scale()
                            .fractional_scale(),
                    )
                    .to_i32_round(),
            );
            let window_geo = window.geometry();
            if window_geo != output_geo {
                let _ = window.configure(output_geo);
                let _ = self.xwm_state(id).raise_window(&window);
                self.state.space.map_element(elem.clone(), (0, 0), true);
                self.state.seat.get_keyboard().unwrap().set_focus(
                    &mut self.state,
                    Some(FocusTarget::from(elem)),
                    SERIAL_COUNTER.next_serial(),
                );
            }
        }
    }
    fn unfullscreen_request(&mut self, _: XwmId, window: X11Surface) {
        let _ = window.set_fullscreen(false);
    }
}
