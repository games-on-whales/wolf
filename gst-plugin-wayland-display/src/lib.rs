use gst::glib;

pub mod utils;
mod waylandsrc;

fn plugin_init(plugin: &gst::Plugin) -> Result<(), glib::BoolError> {
    waylandsrc::register(plugin)?;
    Ok(())
}

gst::plugin_define!(
    waylanddisplaysrc,
    env!("CARGO_PKG_DESCRIPTION"),
    plugin_init,
    concat!(env!("CARGO_PKG_VERSION"), "-", env!("COMMIT_ID")),
    "MIT",
    env!("CARGO_PKG_NAME"),
    env!("CARGO_PKG_NAME"),
    env!("CARGO_PKG_REPOSITORY"),
    env!("BUILD_REL_DATE")
);
