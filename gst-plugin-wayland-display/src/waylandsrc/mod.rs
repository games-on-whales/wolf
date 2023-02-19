use gst::glib;
use gst::prelude::*;

mod comp;
mod imp;

glib::wrapper! {
    pub struct WaylandDisplaySrc(ObjectSubclass<imp::WaylandDisplaySrc>) @extends gst_base::PushSrc, gst_base::BaseSrc, gst::Element, gst::Object;
}

pub fn register(plugin: &gst::Plugin) -> Result<(), glib::BoolError> {
    gst::Element::register(
        Some(plugin),
        "waylanddisplaysrc",
        gst::Rank::Marginal,
        WaylandDisplaySrc::static_type(),
    )
}
