use gst::glib;
use once_cell::sync::Lazy;

pub static CAT: Lazy<gst::DebugCategory> = Lazy::new(|| {
    gst::DebugCategory::new(
        "waylanddisplaysrc",
        gst::DebugColorFlags::empty(),
        Some("Wayland Display Source Bin"),
    )
});

pub struct SlogGstDrain;

impl slog::Drain for SlogGstDrain {
    type Ok = ();
    type Err = std::convert::Infallible;

    fn log(
        &self,
        record: &slog::Record,
        _values: &slog::OwnedKVList,
    ) -> std::result::Result<Self::Ok, Self::Err> {
        CAT.log(
            Option::<&crate::waylandsrc::WaylandDisplaySrc>::None,
            match record.level() {
                slog::Level::Critical | slog::Level::Error => gst::DebugLevel::Error,
                slog::Level::Warning => gst::DebugLevel::Warning,
                slog::Level::Info => gst::DebugLevel::Info,
                slog::Level::Debug => gst::DebugLevel::Debug,
                slog::Level::Trace => gst::DebugLevel::Trace,
            },
            glib::GString::from(record.file()).as_gstr(),
            record.module(),
            record.line(),
            *record.msg(),
        );
        Ok(())
    }
}
