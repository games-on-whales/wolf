use std::fmt::{self, Write};

use gst::glib;
use once_cell::sync::Lazy;
use tracing::field::{Field, Visit};

pub static CAT: Lazy<gst::DebugCategory> = Lazy::new(|| {
    gst::DebugCategory::new(
        "waylanddisplaysrc",
        gst::DebugColorFlags::empty(),
        Some("Wayland Display Source Bin"),
    )
});

#[derive(Debug, Clone)]
pub struct GstLayer;

pub struct StringVisitor<'a> {
    string: &'a mut String,
}

impl<'a> Visit for StringVisitor<'a> {
    fn record_debug(&mut self, field: &Field, value: &dyn fmt::Debug) {
        write!(self.string, "{} = {:?}; ", field.name(), value).unwrap();
    }
}

impl<S> tracing_subscriber::Layer<S> for GstLayer
where
    S: tracing::Subscriber,
{
    fn on_event(
        &self,
        event: &tracing::Event<'_>,
        _ctx: tracing_subscriber::layer::Context<'_, S>,
    ) {
        let mut message = String::new();
        event.record(&mut StringVisitor {
            string: &mut message,
        });

        CAT.log(
            Option::<&crate::waylandsrc::WaylandDisplaySrc>::None,
            match event.metadata().level() {
                &tracing::Level::ERROR => gst::DebugLevel::Error,
                &tracing::Level::WARN => gst::DebugLevel::Warning,
                &tracing::Level::INFO => gst::DebugLevel::Info,
                &tracing::Level::DEBUG => gst::DebugLevel::Debug,
                &tracing::Level::TRACE => gst::DebugLevel::Trace,
            },
            glib::GString::from(event.metadata().file().unwrap_or("<unknown file>")).as_gstr(),
            event.metadata().module_path().unwrap_or("<unknown module>"),
            event.metadata().line().unwrap_or(0),
            format_args!("{}", message),
        );
    }
}
