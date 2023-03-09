use gst::ffi::GstBuffer;
use gst::glib::translate::FromGlibPtrNone;
use gst_video::ffi::GstVideoInfo;
use gst_video::VideoInfo;

use smithay::backend::drm::CreateDrmNodeError;
use smithay::backend::SwapBuffersError;
use smithay::reexports::calloop::channel::Sender;

use std::ffi::{c_char, CStr, CString};
use std::ptr;
use std::str::FromStr;
use std::sync::mpsc::{self, Receiver, SyncSender};
use std::thread::JoinHandle;

use utils::RenderTarget;

pub(crate) mod comp;
pub(crate) mod utils;
pub(crate) mod wayland;

pub(crate) enum Command {
    InputDevice(String),
    VideoInfo(VideoInfo),
    Buffer(SyncSender<Result<gst::Buffer, SwapBuffersError>>),
    Quit,
}

pub struct WaylandDisplay {
    thread_handle: Option<JoinHandle<()>>,
    command_tx: Sender<Command>,

    devices: MaybeRecv<Vec<CString>>,
    envs: MaybeRecv<Vec<CString>>,
}

enum MaybeRecv<T: Clone> {
    Rx(Receiver<T>),
    Value(T),
}

impl<T: Clone> MaybeRecv<T> {
    fn get(&mut self) -> &T {
        match self {
            MaybeRecv::Rx(recv) => {
                let value = recv.recv().unwrap();
                *self = MaybeRecv::Value(value.clone());
                self.get()
            }
            MaybeRecv::Value(val) => val,
        }
    }
}

impl WaylandDisplay {
    pub fn new(render_node: Option<String>) -> Result<WaylandDisplay, CreateDrmNodeError> {
        let (channel_tx, channel_rx) = std::sync::mpsc::sync_channel(0);
        let (devices_tx, devices_rx) = std::sync::mpsc::channel();
        let (envs_tx, envs_rx) = std::sync::mpsc::channel();
        let render_target = RenderTarget::from_str(
            &render_node.unwrap_or_else(|| String::from("/dev/dri/renderD128")),
        )?;

        let thread_handle = std::thread::spawn(move || {
            if let Err(err) = std::panic::catch_unwind(|| {
                // calloops channel is not "UnwindSafe", but the std channel is... *sigh* lets workaround it creatively
                let (command_tx, command_src) = smithay::reexports::calloop::channel::channel();
                channel_tx.send(command_tx).unwrap();
                comp::init(command_src, render_target, devices_tx, envs_tx);
            }) {
                tracing::error!(?err, "Compositor thread panic'ed!");
            }
        });
        let command_tx = channel_rx.recv().unwrap();

        Ok(WaylandDisplay {
            thread_handle: Some(thread_handle),
            command_tx,
            devices: MaybeRecv::Rx(devices_rx),
            envs: MaybeRecv::Rx(envs_rx),
        })
    }

    pub fn devices(&mut self) -> impl Iterator<Item = &str> {
        self.devices
            .get()
            .iter()
            .map(|string| string.to_str().unwrap())
    }

    pub fn env_vars(&mut self) -> impl Iterator<Item = &str> {
        self.envs
            .get()
            .iter()
            .map(|string| string.to_str().unwrap())
    }

    pub fn add_input_device(&self, path: impl Into<String>) {
        let _ = self.command_tx.send(Command::InputDevice(path.into()));
    }

    pub fn set_video_info(&self, info: VideoInfo) {
        let _ = self.command_tx.send(Command::VideoInfo(info));
    }

    pub fn frame(&self) -> Result<gst::Buffer, gst::FlowError> {
        let (buffer_tx, buffer_rx) = mpsc::sync_channel(0);
        if let Err(err) = self.command_tx.send(Command::Buffer(buffer_tx)) {
            tracing::warn!(?err, "Failed to send buffer command.");
            return Err(gst::FlowError::Eos);
        }

        match buffer_rx.recv() {
            Ok(Ok(buffer)) => Ok(buffer),
            Ok(Err(err)) => match err {
                SwapBuffersError::AlreadySwapped => unreachable!(),
                SwapBuffersError::ContextLost(_) => Err(gst::FlowError::Eos),
                SwapBuffersError::TemporaryFailure(_) => Err(gst::FlowError::Error),
            },
            Err(err) => {
                tracing::warn!(?err, "Failed to recv buffer ack.");
                Err(gst::FlowError::Error)
            }
        }
    }
}

impl Drop for WaylandDisplay {
    fn drop(&mut self) {
        if let Err(err) = self.command_tx.send(Command::Quit) {
            tracing::warn!("Failed to send stop command: {}", err);
            return;
        };
        if self.thread_handle.take().unwrap().join().is_err() {
            tracing::warn!("Failed to join compositor thread");
        };
    }
}

// C API

#[no_mangle]
pub extern "C" fn display_init(render_node: *const c_char) -> *mut WaylandDisplay {
    let render_node = if !render_node.is_null() {
        Some(
            unsafe { CStr::from_ptr(render_node) }
                .to_string_lossy()
                .into_owned(),
        )
    } else {
        None
    };

    match WaylandDisplay::new(render_node) {
        Ok(dpy) => Box::into_raw(Box::new(dpy)),
        Err(err) => {
            tracing::error!(?err, "Failed to create wayland display.");
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn display_finish(dpy: *mut WaylandDisplay) {
    std::mem::drop(unsafe { Box::from_raw(dpy) })
}

#[no_mangle]
pub extern "C" fn display_get_devices_len(dpy: *mut WaylandDisplay) -> usize {
    let display = unsafe { &mut *dpy };
    display.devices.get().len()
}

#[no_mangle]
pub extern "C" fn display_get_devices(
    dpy: *mut WaylandDisplay,
    devices: *mut *const c_char,
    max_len: usize,
) -> usize {
    let display = unsafe { &mut *dpy };
    let client_devices = unsafe { std::slice::from_raw_parts_mut(devices, max_len) };
    let devices = display.devices.get();

    for (i, string) in devices.iter().take(max_len).enumerate() {
        client_devices[i] = string.as_ptr() as *const _;
    }

    std::cmp::max(max_len, devices.len())
}

#[no_mangle]
pub extern "C" fn display_get_envvars_len(dpy: *mut WaylandDisplay) -> usize {
    let display = unsafe { &mut *dpy };
    display.envs.get().len()
}

#[no_mangle]
pub extern "C" fn display_get_envvars(
    dpy: *mut WaylandDisplay,
    env_vars: *mut *const c_char,
    max_len: usize,
) -> usize {
    let display = unsafe { &mut *dpy };
    let client_env_vars = unsafe { std::slice::from_raw_parts_mut(env_vars, max_len) };
    let env_vars = display.envs.get();

    for (i, string) in env_vars.iter().take(max_len).enumerate() {
        client_env_vars[i] = string.as_ptr() as *const _;
    }

    std::cmp::max(max_len, env_vars.len())
}

#[no_mangle]
pub extern "C" fn display_add_input_device(dpy: *mut WaylandDisplay, path: *const c_char) {
    let display = unsafe { &mut *dpy };
    let path = unsafe { CStr::from_ptr(path) }
        .to_string_lossy()
        .into_owned();

    display.add_input_device(path);
}

#[no_mangle]
pub extern "C" fn display_set_video_info(dpy: *mut WaylandDisplay, info: *const GstVideoInfo) {
    let display = unsafe { &mut *dpy };
    if info.is_null() {
        tracing::error!("Video Info is null");
    }
    let video_info = unsafe { VideoInfo::from_glib_none(info) };

    display.set_video_info(video_info);
}

#[no_mangle]
pub extern "C" fn display_get_frame(dpy: *mut WaylandDisplay) -> *mut GstBuffer {
    let display = unsafe { &mut *dpy };
    match display.frame() {
        Ok(mut frame) => {
            let ptr = frame.make_mut().as_mut_ptr();
            std::mem::forget(frame);
            ptr
        }
        Err(err) => {
            tracing::error!("Rendering error: {}", err);
            ptr::null_mut()
        }
    }
}
