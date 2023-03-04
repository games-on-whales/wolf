use gst::glib;

pub mod protocols;
pub mod utils;
mod waylandsrc;

fn plugin_init(plugin: &gst::Plugin) -> Result<(), glib::BoolError> {
    waylandsrc::register(plugin)?;
    Ok(())
}

gst::plugin_define!(
    waylanddisplay,
    env!("CARGO_PKG_DESCRIPTION"),
    plugin_init,
    concat!(env!("CARGO_PKG_VERSION"), "-", env!("COMMIT_ID")),
    "MPL",
    env!("CARGO_PKG_NAME"),
    env!("CARGO_PKG_NAME"),
    env!("CARGO_PKG_REPOSITORY"),
    env!("BUILD_REL_DATE")
);
