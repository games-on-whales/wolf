use std::sync::Mutex;

use gst::message::Application;
use gst_video::{VideoCapsBuilder, VideoFormat};

use gst::subclass::prelude::*;
use gst::{glib, Event, Fraction};
use gst::{
    glib::{once_cell::sync::Lazy, ValueArray},
    LibraryError,
};
use gst::{prelude::*, Structure};

use gst_base::subclass::base_src::CreateSuccess;
use gst_base::subclass::prelude::*;
use gst_base::traits::BaseSrcExt;

use gstwaylanddisplay::WaylandDisplay;
use tracing_subscriber::layer::SubscriberExt;
use tracing_subscriber::Registry;

use crate::utils::{GstLayer, CAT};

pub struct WaylandDisplaySrc {
    state: Mutex<Option<State>>,
    settings: Mutex<Settings>,
}

impl Default for WaylandDisplaySrc {
    fn default() -> Self {
        WaylandDisplaySrc {
            state: Mutex::new(None),
            settings: Mutex::new(Settings::default()),
        }
    }
}

#[derive(Debug, Default)]
pub struct Settings {
    render_node: Option<String>,
}

pub struct State {
    display: WaylandDisplay,
}

#[glib::object_subclass]
impl ObjectSubclass for WaylandDisplaySrc {
    const NAME: &'static str = "GstWaylandDisplaySrc";
    type Type = super::WaylandDisplaySrc;
    type ParentType = gst_base::PushSrc;
    type Interfaces = ();
}

impl ObjectImpl for WaylandDisplaySrc {
    fn properties() -> &'static [glib::ParamSpec] {
        static PROPERTIES: Lazy<Vec<glib::ParamSpec>> = Lazy::new(|| {
            vec![glib::ParamSpecString::builder("render-node")
                .nick("DRM Render Node")
                .blurb("DRM Render Node to use (e.g. /dev/dri/renderD128")
                .construct()
                .build()]
        });

        PROPERTIES.as_ref()
    }

    fn set_property(&self, _id: usize, value: &glib::Value, pspec: &glib::ParamSpec) {
        match pspec.name() {
            "render-node" => {
                let mut settings = self.settings.lock().unwrap();
                settings.render_node = value
                    .get::<Option<String>>()
                    .expect("Type checked upstream");
            }
            _ => unreachable!(),
        }
    }

    fn property(&self, _id: usize, pspec: &glib::ParamSpec) -> glib::Value {
        match pspec.name() {
            "render-node" => {
                let settings = self.settings.lock().unwrap();
                settings
                    .render_node
                    .clone()
                    .unwrap_or_else(|| String::from("/dev/dri/renderD128"))
                    .to_value()
            }
            _ => unreachable!(),
        }
    }

    fn constructed(&self) {
        self.parent_constructed();

        let obj = self.obj();
        obj.set_element_flags(gst::ElementFlags::SOURCE);
        obj.set_live(true);
        obj.set_format(gst::Format::Time);
        obj.set_automatic_eos(false);
        obj.set_do_timestamp(true);
    }
}

impl GstObjectImpl for WaylandDisplaySrc {}

impl ElementImpl for WaylandDisplaySrc {
    fn metadata() -> Option<&'static gst::subclass::ElementMetadata> {
        static ELEMENT_METADATA: Lazy<gst::subclass::ElementMetadata> = Lazy::new(|| {
            gst::subclass::ElementMetadata::new(
                "Wayland display source",
                "Source/Video",
                "GStreamer video src running a wayland compositor",
                "Victoria Brekenfeld <wayland@drakulix.de>",
            )
        });

        Some(&*ELEMENT_METADATA)
    }

    fn pad_templates() -> &'static [gst::PadTemplate] {
        static PAD_TEMPLATES: Lazy<Vec<gst::PadTemplate>> = Lazy::new(|| {
            let caps = gst_video::VideoCapsBuilder::new()
                .format(VideoFormat::Rgbx)
                .height_range(..i32::MAX)
                .width_range(..i32::MAX)
                .framerate_range(Fraction::new(1, 1)..Fraction::new(i32::MAX, 1))
                .build();
            let src_pad_template = gst::PadTemplate::new(
                "src",
                gst::PadDirection::Src,
                gst::PadPresence::Always,
                &caps,
            )
            .unwrap();

            vec![src_pad_template]
        });

        PAD_TEMPLATES.as_ref()
    }

    fn change_state(
        &self,
        transition: gst::StateChange,
    ) -> Result<gst::StateChangeSuccess, gst::StateChangeError> {
        let res = self.parent_change_state(transition);
        match res {
            Ok(gst::StateChangeSuccess::Success) => {
                if transition.next() == gst::State::Paused {
                    // this is a live source
                    Ok(gst::StateChangeSuccess::NoPreroll)
                } else {
                    Ok(gst::StateChangeSuccess::Success)
                }
            }
            x => x,
        }
    }

    fn query(&self, query: &mut gst::QueryRef) -> bool {
        ElementImplExt::parent_query(self, query)
    }
}

impl BaseSrcImpl for WaylandDisplaySrc {
    fn query(&self, query: &mut gst::QueryRef) -> bool {
        BaseSrcImplExt::parent_query(self, query)
    }

    fn caps(&self, filter: Option<&gst::Caps>) -> Option<gst::Caps> {
        let mut caps = VideoCapsBuilder::new()
            .format(VideoFormat::Rgbx)
            .height_range(..i32::MAX)
            .width_range(..i32::MAX)
            .framerate_range(Fraction::new(1, 1)..Fraction::new(i32::MAX, 1))
            .build();

        if let Some(filter) = filter {
            caps = caps.intersect(filter);
        }

        Some(caps)
    }

    fn negotiate(&self) -> Result<(), gst::LoggableError> {
        self.parent_negotiate()
    }

    fn event(&self, event: &Event) -> bool {
        if event.type_() == gst::EventType::CustomUpstream {
            let structure = event.structure().expect("Unable to get message structure");
            if structure.has_name("VirtualDevicesReady") {
                let mut state = self.state.lock().unwrap();
                let display = &mut state.as_mut().unwrap().display;

                let paths = structure
                    .get::<ValueArray>("paths")
                    .expect("Should contain paths");
                for value in paths.into_iter() {
                    let path = value.get::<String>().expect("Paths are strings");
                    display.add_input_device(path);
                }

                return true;
            }
        }
        self.parent_event(event)
    }

    fn set_caps(&self, caps: &gst::Caps) -> Result<(), gst::LoggableError> {
        let video_info = gst_video::VideoInfo::from_caps(caps).expect("failed to get video info");
        self.state
            .lock()
            .unwrap()
            .as_mut()
            .unwrap()
            .display
            .set_video_info(video_info);

        self.parent_set_caps(caps)
    }

    fn start(&self) -> Result<(), gst::ErrorMessage> {
        let mut state = self.state.lock().unwrap();
        if state.is_some() {
            return Ok(());
        }

        let settings = self.settings.lock().unwrap();
        let elem = self.obj().upcast_ref::<gst::Element>().to_owned();
        let subscriber = Registry::default().with(GstLayer);

        let Ok(mut display) = tracing::subscriber::with_default(subscriber, || WaylandDisplay::new(settings.render_node.clone())) else {
            return Err(gst::error_msg!(LibraryError::Failed, ("Failed to open drm node {}, if you want to utilize software rendering set `render-node=software`.", settings.render_node.as_deref().unwrap_or("/dev/dri/renderD128"))));
        };

        let mut structure = Structure::builder("wayland.src");
        for (key, var) in display.env_vars().flat_map(|var| var.split_once("=")) {
            structure = structure.field(key, var);
        }
        let structure = structure.build();
        if let Err(err) = elem.post_message(Application::builder(structure).src(&elem).build()) {
            gst::warning!(CAT, "Failed to post environment to gstreamer bus: {}", err);
        }

        *state = Some(State { display });

        Ok(())
    }

    fn stop(&self) -> Result<(), gst::ErrorMessage> {
        let mut state = self.state.lock().unwrap();
        if let Some(state) = state.take() {
            let subscriber = Registry::default().with(GstLayer);
            tracing::subscriber::with_default(subscriber, || std::mem::drop(state.display));
        }
        Ok(())
    }

    fn is_seekable(&self) -> bool {
        false
    }
}

impl PushSrcImpl for WaylandDisplaySrc {
    fn create(
        &self,
        _buffer: Option<&mut gst::BufferRef>,
    ) -> Result<CreateSuccess, gst::FlowError> {
        let mut state_guard = self.state.lock().unwrap();
        let Some(state) = state_guard.as_mut() else {
            return Err(gst::FlowError::Eos);
        };

        let subscriber = Registry::default().with(GstLayer);
        tracing::subscriber::with_default(subscriber, || {
            state.display.frame().map(CreateSuccess::NewBuffer)
        })
    }
}
