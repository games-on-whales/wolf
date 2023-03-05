use std::time::Duration;

use smithay::{
    backend::renderer::{
        element::{surface::WaylandSurfaceRenderElement, AsRenderElements},
        ImportAll, Renderer,
    },
    desktop::{utils::send_frames_surface_tree, Window as WaylandWindow},
    output::Output,
    reexports::wayland_server::{backend::ObjectId, protocol::wl_surface::WlSurface},
    space_elements,
    utils::{Physical, Point},
    wayland::{compositor::SurfaceData, seat::WaylandFocus, shell::xdg::ToplevelSurface},
    xwayland::X11Surface,
};

space_elements! {
    #[derive(Debug, Clone, PartialEq)]
    pub Window;
    Wayland=WaylandWindow,
    X11=X11Surface,
}

impl From<ToplevelSurface> for Window {
    fn from(s: ToplevelSurface) -> Self {
        Window::Wayland(WaylandWindow::new(s))
    }
}

impl From<X11Surface> for Window {
    fn from(s: X11Surface) -> Self {
        Window::X11(s)
    }
}

impl WaylandFocus for Window {
    fn wl_surface(&self) -> Option<WlSurface> {
        match self {
            Window::Wayland(w) => w.wl_surface(),
            Window::X11(w) => w.wl_surface(),
            _ => unreachable!(),
        }
    }

    fn same_client_as(&self, object_id: &ObjectId) -> bool {
        match self {
            Window::Wayland(w) => w.same_client_as(object_id),
            Window::X11(w) => w.same_client_as(object_id),
            _ => unreachable!(),
        }
    }
}

impl<R> AsRenderElements<R> for Window
where
    R: Renderer + ImportAll,
    <R as Renderer>::TextureId: 'static,
{
    type RenderElement = WaylandSurfaceRenderElement<R>;
    fn render_elements<C: From<Self::RenderElement>>(
        &self,
        renderer: &mut R,
        location: Point<i32, Physical>,
        scale: smithay::utils::Scale<f64>,
    ) -> Vec<C> {
        match self {
            Window::Wayland(w) => w.render_elements(renderer, location, scale),
            Window::X11(s) => s.render_elements(renderer, location, scale),
            _ => unreachable!(),
        }
    }
}

impl Window {
    pub fn on_commit(&self) {
        match self {
            Window::Wayland(w) => w.on_commit(),
            _ => {}
        }
    }

    pub fn send_frame<T, F>(
        &self,
        output: &Output,
        time: T,
        throttle: Option<Duration>,
        primary_scan_out_output: F,
    ) where
        T: Into<Duration>,
        F: FnMut(&WlSurface, &SurfaceData) -> Option<Output> + Copy,
    {
        match self {
            Window::Wayland(w) => w.send_frame(output, time, throttle, primary_scan_out_output),
            Window::X11(s) => {
                if let Some(surface) = s.wl_surface() {
                    send_frames_surface_tree(
                        &surface,
                        output,
                        time,
                        throttle,
                        primary_scan_out_output,
                    )
                }
            }
            _ => unreachable!(),
        }
    }
}
