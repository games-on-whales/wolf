use std::{
    collections::{HashMap, HashSet},
    ffi::CString,
    os::unix::prelude::AsRawFd,
    sync::{mpsc::Sender, Arc, Mutex, Weak},
    time::{Duration, Instant},
};

use super::Command;
use gst_video::VideoInfo;
use once_cell::sync::Lazy;
use smithay::{
    backend::{
        allocator::dmabuf::Dmabuf,
        drm::{DrmNode, NodeType},
        egl::{EGLContext, EGLDevice, EGLDisplay},
        libinput::LibinputInputBackend,
        renderer::{
            damage::{DamageTrackedRenderer, DamageTrackedRendererError as DTRError},
            element::memory::MemoryRenderBuffer,
            gles2::{Gles2Renderbuffer, Gles2Renderer},
            Bind, ImportMemWl, Offscreen,
        },
    },
    desktop::{utils::send_frames_surface_tree, PopupManager, Space, Window},
    input::{keyboard::XkbConfig, pointer::CursorImageStatus, Seat, SeatState},
    output::{Mode as OutputMode, Output, PhysicalProperties, Subpixel},
    reexports::{
        calloop::{
            channel::{Channel, Event},
            generic::Generic,
            timer::{TimeoutAction, Timer},
            EventLoop, Interest, LoopHandle, Mode, PostAction,
        },
        input::Libinput,
        wayland_server::{
            backend::{ClientData, ClientId, DisconnectReason},
            Display, DisplayHandle,
        },
    },
    utils::{Logical, Physical, Point, Rectangle, Size, Transform},
    wayland::{
        compositor::{with_states, CompositorState},
        data_device::DataDeviceState,
        dmabuf::{DmabufGlobal, DmabufState},
        output::OutputManagerState,
        shell::xdg::{XdgShellState, XdgToplevelSurfaceData},
        shm::ShmState,
        socket::ListeningSocketSource,
        viewporter::ViewporterState,
    },
};
use wayland_backend::server::GlobalId;

mod focus;
mod input;
mod rendering;

pub use self::focus::*;
pub use self::input::*;
pub use self::rendering::*;
use crate::{utils::RenderTarget, wayland::protocols::wl_drm::create_drm_global};

static EGL_DISPLAYS: Lazy<Mutex<HashMap<Option<DrmNode>, Weak<EGLDisplay>>>> =
    Lazy::new(|| Mutex::new(HashMap::new()));

struct ClientState;
impl ClientData for ClientState {
    fn initialized(&self, _client_id: ClientId) {}
    fn disconnected(&self, _client_id: ClientId, _reason: DisconnectReason) {}
}

pub(crate) struct Data {
    pub(crate) display: Display<State>,
    pub(crate) state: State,
}

#[allow(dead_code)]
pub(crate) struct State {
    handle: LoopHandle<'static, Data>,
    should_quit: bool,
    start_time: std::time::Instant,

    // render
    dtr: Option<DamageTrackedRenderer>,
    renderbuffer: Option<Gles2Renderbuffer>,
    pub renderer: Gles2Renderer,
    egl_display_ref: Arc<EGLDisplay>,
    dmabuf_global: Option<(DmabufGlobal, GlobalId)>,
    last_render: Option<Instant>,

    // management
    pub output: Option<Output>,
    pub video_info: Option<VideoInfo>,
    pub seat: Seat<Self>,
    pub space: Space<Window>,
    pub popups: PopupManager,
    pointer_location: Point<f64, Logical>,
    last_pointer_movement: Instant,
    cursor_element: MemoryRenderBuffer,
    pub cursor_state: CursorImageStatus,
    surpressed_keys: HashSet<u32>,
    pub pending_windows: Vec<Window>,
    input_context: Libinput,

    // wayland state
    pub dh: DisplayHandle,
    pub compositor_state: CompositorState,
    pub data_device_state: DataDeviceState,
    pub dmabuf_state: DmabufState,
    output_state: OutputManagerState,
    pub seat_state: SeatState<Self>,
    pub shell_state: XdgShellState,
    pub shm_state: ShmState,
    viewporter_state: ViewporterState,
}

pub fn get_egl_device_for_node(drm_node: &DrmNode) -> EGLDevice {
    let drm_node = drm_node
        .node_with_type(NodeType::Render)
        .and_then(Result::ok)
        .unwrap_or(drm_node.clone());
    EGLDevice::enumerate()
        .expect("Failed to enumerate EGLDevices")
        .find(|d| d.try_get_render_node().unwrap_or_default() == Some(drm_node))
        .expect("Unable to find EGLDevice for drm-node")
}

pub(crate) fn init(
    command_src: Channel<Command>,
    render: impl Into<RenderTarget>,
    devices_tx: Sender<Vec<CString>>,
    envs_tx: Sender<Vec<CString>>,
) {
    let mut display = Display::<State>::new().unwrap();
    let dh = display.handle();

    // init state
    let compositor_state = CompositorState::new::<State>(&dh);
    let data_device_state = DataDeviceState::new::<State>(&dh);
    let mut dmabuf_state = DmabufState::new();
    let output_state = OutputManagerState::new_with_xdg_output::<State>(&dh);
    let mut seat_state = SeatState::new();
    let shell_state = XdgShellState::new::<State>(&dh);
    let viewporter_state = ViewporterState::new::<State>(&dh);

    let render_target = render.into();
    let render_node: Option<DrmNode> = render_target.clone().into();

    // init render backend
    let (egl_display_ref, context) = {
        let mut displays = EGL_DISPLAYS.lock().unwrap();
        let maybe_display = displays
            .get(&render_node)
            .and_then(|weak_display| weak_display.upgrade());

        let egl = match maybe_display {
            Some(display) => display,
            None => {
                let device = match render_node.as_ref() {
                    Some(render_node) => get_egl_device_for_node(render_node),
                    None => EGLDevice::enumerate()
                        .expect("Failed to enumerate EGLDevices")
                        .find(|device| {
                            device
                                .extensions()
                                .iter()
                                .any(|e| e == "EGL_MESA_device_software")
                        })
                        .expect("Failed to find software device"),
                };
                let display =
                    Arc::new(EGLDisplay::new(device).expect("Failed to initialize EGL display"));
                displays.insert(render_node, Arc::downgrade(&display));
                display
            }
        };
        let context = EGLContext::new(&egl).expect("Failed to initialize EGL context");
        (egl, context)
    };
    let renderer = unsafe { Gles2Renderer::new(context) }.expect("Failed to initialize renderer");
    let _ = devices_tx.send(render_target.as_devices());

    let shm_state = ShmState::new::<State>(&dh, Vec::from(renderer.shm_formats()));
    let dmabuf_global = if let RenderTarget::Hardware(node) = render_target {
        let formats = Bind::<Dmabuf>::supported_formats(&renderer)
            .expect("Failed to query formats")
            .into_iter()
            .collect::<Vec<_>>();

        // dma buffer
        let dmabuf_global = dmabuf_state.create_global::<State>(&dh, formats.clone());
        // wl_drm (mesa protocol, so we don't need EGL_WL_bind_display)
        let wl_drm_global = create_drm_global::<State>(
            &dh,
            node.dev_path().expect("Failed to determine DrmNode path?"),
            formats.clone(),
            &dmabuf_global,
        );

        Some((dmabuf_global, wl_drm_global))
    } else {
        None
    };

    let cursor_element =
        MemoryRenderBuffer::from_memory(CURSOR_DATA_BYTES, (64, 64), 1, Transform::Normal, None);

    // init input backend
    let libinput_context = Libinput::new_from_path(NixInterface);
    let input_context = libinput_context.clone();
    let libinput_backend = LibinputInputBackend::new(libinput_context);

    let space = Space::default();

    let mut seat = seat_state.new_wl_seat(&dh, "seat-0");
    seat.add_keyboard(XkbConfig::default(), 200, 25)
        .expect("Failed to add keyboard to seat");
    seat.add_pointer();

    let mut event_loop =
        EventLoop::<Data>::try_new_high_precision().expect("Unable to create event_loop");
    let state = State {
        handle: event_loop.handle(),
        should_quit: false,
        start_time: std::time::Instant::now(),

        renderer,
        egl_display_ref,
        dtr: None,
        renderbuffer: None,
        dmabuf_global,
        video_info: None,
        last_render: None,

        space,
        popups: PopupManager::default(),
        seat,
        output: None,
        pointer_location: (0., 0.).into(),
        last_pointer_movement: Instant::now(),
        cursor_element,
        cursor_state: CursorImageStatus::Default,
        surpressed_keys: HashSet::new(),
        pending_windows: Vec::new(),
        input_context,

        dh: display.handle(),
        compositor_state,
        data_device_state,
        dmabuf_state,
        output_state,
        seat_state,
        shell_state,
        shm_state,
        viewporter_state,
    };

    // init event loop
    event_loop
        .handle()
        .insert_source(libinput_backend, move |event, _, data| {
            data.state.process_input_event(event)
        })
        .unwrap();

    event_loop
        .handle()
        .insert_source(command_src, move |event, _, data| {
            match event {
                Event::Msg(Command::VideoInfo(info)) => {
                    let size: Size<i32, Physical> =
                        (info.width() as i32, info.height() as i32).into();
                    let framerate = info.fps();
                    let duration = Duration::from_secs_f64(
                        framerate.numer() as f64 / framerate.denom() as f64,
                    );

                    // init wayland objects
                    let output = data.state.output.get_or_insert_with(|| {
                        let output = Output::new(
                            "HEADLESS-1".into(),
                            PhysicalProperties {
                                make: "Virtual".into(),
                                model: "Wolf".into(),
                                size: (0, 0).into(),
                                subpixel: Subpixel::Unknown,
                            },
                        );
                        output.create_global::<State>(&data.display.handle());
                        output
                    });
                    let mode = OutputMode {
                        size: size.into(),
                        refresh: (duration.as_secs_f64() * 1000.0).round() as i32,
                    };
                    output.change_current_state(Some(mode), None, None, None);
                    output.set_preferred(mode);
                    let dtr = DamageTrackedRenderer::from_output(&output);

                    data.state.space.map_output(&output, (0, 0));
                    data.state.dtr = Some(dtr);
                    data.state.pointer_location = (size.w as f64 / 2.0, size.h as f64 / 2.0).into();
                    data.state.renderbuffer = Some(
                        data.state
                            .renderer
                            .create_buffer((info.width() as i32, info.height() as i32).into())
                            .expect("Failed to create renderbuffer"),
                    );
                    data.state.video_info = Some(info);

                    let new_size = size
                        .to_f64()
                        .to_logical(output.current_scale().fractional_scale())
                        .to_i32_round();
                    for window in data.state.space.elements() {
                        let toplevel = window.toplevel();
                        let max_size = Rectangle::from_loc_and_size(
                            (0, 0),
                            with_states(toplevel.wl_surface(), |states| {
                                states
                                    .data_map
                                    .get::<XdgToplevelSurfaceData>()
                                    .map(|attrs| attrs.lock().unwrap().max_size)
                            })
                            .unwrap_or(new_size),
                        );

                        let new_size = max_size
                            .intersection(Rectangle::from_loc_and_size((0, 0), new_size))
                            .map(|rect| rect.size);
                        toplevel.with_pending_state(|state| state.size = new_size);
                        toplevel.send_configure();
                    }
                }
                Event::Msg(Command::InputDevice(path)) => {
                    tracing::info!(path, "Adding input device.");
                    data.state.input_context.path_add_device(&path);
                }
                Event::Msg(Command::Buffer(buffer_sender)) => {
                    let wait = if let Some(last_render) = data.state.last_render {
                        let framerate = data.state.video_info.as_ref().unwrap().fps();
                        let duration = Duration::from_secs_f64(
                            framerate.denom() as f64 / framerate.numer() as f64,
                        );
                        let time_passed = Instant::now().duration_since(last_render);
                        if time_passed < duration {
                            Some(duration - time_passed)
                        } else {
                            None
                        }
                    } else {
                        None
                    };

                    let render = move |data: &mut Data, now: Instant| {
                        if let Err(_) = match data.state.create_frame() {
                            Ok(buf) => {
                                data.state.last_render = Some(now);
                                buffer_sender.send(Ok(buf))
                            }
                            Err(err) => {
                                tracing::error!(?err, "Rendering failed.");
                                buffer_sender.send(Err(match err {
                                    DTRError::OutputNoMode(_) => unreachable!(),
                                    DTRError::Rendering(err) => err.into(),
                                }))
                            }
                        } {
                            data.state.should_quit = true;
                        }
                    };

                    match wait {
                        Some(duration) => {
                            if let Err(err) = data.state.handle.insert_source(
                                Timer::from_duration(duration),
                                move |now, _, data| {
                                    render(data, now);
                                    TimeoutAction::Drop
                                },
                            ) {
                                tracing::error!(?err, "Event loop error.");
                                data.state.should_quit = true;
                            };
                        }
                        None => render(data, Instant::now()),
                    };
                }
                Event::Msg(Command::Quit) | Event::Closed => {
                    data.state.should_quit = true;
                }
            };
        })
        .unwrap();

    let source = ListeningSocketSource::new_auto().unwrap();
    let socket_name = source.socket_name().to_string_lossy().into_owned();
    tracing::info!(?socket_name, "Listening on wayland socket.");
    event_loop
        .handle()
        .insert_source(source, |client_stream, _, data| {
            if let Err(err) = data
                .display
                .handle()
                .insert_client(client_stream, std::sync::Arc::new(ClientState))
            {
                tracing::error!(?err, "Error adding wayland client.");
            };
        })
        .expect("Failed to init wayland socket source");

    event_loop
        .handle()
        .insert_source(
            Generic::new(
                display.backend().poll_fd().as_raw_fd(),
                Interest::READ,
                Mode::Level,
            ),
            |_, _, data| {
                data.display.dispatch_clients(&mut data.state).unwrap();
                Ok(PostAction::Continue)
            },
        )
        .expect("Failed to init wayland server source");

    let env_vars = vec![CString::new(format!("WAYLAND_DISPLAY={}", socket_name)).unwrap()];
    if let Err(err) = envs_tx.send(env_vars) {
        tracing::warn!(?err, "Failed to post environment to application.");
    }

    let mut data = Data { display, state };
    let signal = event_loop.get_signal();
    if let Err(err) = event_loop.run(None, &mut data, |data| {
        if let Some(output) = data.state.output.as_ref() {
            for window in data.state.space.elements() {
                window.send_frame(
                    output,
                    data.state.start_time.elapsed(),
                    Some(Duration::ZERO),
                    |_, _| Some(output.clone()),
                )
            }
            if let CursorImageStatus::Surface(wl_surface) = &data.state.cursor_state {
                send_frames_surface_tree(
                    wl_surface,
                    output,
                    data.state.start_time.elapsed(),
                    None,
                    |_, _| Some(output.clone()),
                )
            }
        }

        data.display
            .flush_clients()
            .expect("Failed to flush clients");
        data.state.space.refresh();
        data.state.popups.cleanup();

        if data.state.should_quit {
            signal.stop();
        }
    }) {
        tracing::error!(?err, "Event loop broke.");
    }
}
