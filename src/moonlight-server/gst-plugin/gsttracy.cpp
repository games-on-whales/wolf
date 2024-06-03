#include "gsttracy.hpp"
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <tracy/TracyC.h>

GST_DEBUG_CATEGORY_STATIC(gst_tracy_debug);
#define GST_CAT_DEFAULT gst_tracy_debug

thread_local std::map<std::string, TracyCZoneCtx> zones;

G_DEFINE_TYPE(GstTracyTracer, gst_tracy_tracer, GST_TYPE_TRACER);

static void gst_tracy_tracer_class_init(GstTracyTracerClass *klass) {
  GObjectClass *oclass = G_OBJECT_CLASS(klass);

  GST_DEBUG_CATEGORY_INIT(gst_tracy_debug, "tracytracer", 0, "base tracy tracer");
}

GstPad *get_source_pad(GstPad *pad) {
  while (pad) {
    if (GST_IS_GHOST_PAD(pad)) {
      pad = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
    } else if (GST_OBJECT_PARENT(pad) && GST_IS_GHOST_PAD(GST_OBJECT_PARENT(pad))) {
      pad = GST_PAD_PEER(GST_OBJECT_PARENT(pad));
    } else {
      break;
    }
  }
  return pad;
}

static GstElement *get_real_pad_parent(GstPad *pad) {
  GstObject *parent;

  if (!pad)
    return NULL;

  parent = GST_OBJECT_PARENT(pad);

  /* if parent of pad is a ghost-pad, then pad is a proxy_pad */
  if (parent && GST_IS_GHOST_PAD(parent)) {
    pad = GST_PAD_CAST(parent);
    parent = GST_OBJECT_PARENT(pad);
  }
  return GST_ELEMENT_CAST(parent);
}

static std::optional<std::string> get_unique_name(GstPad *sender_pad, bool is_push = true) {
  if (GST_IS_GHOST_PAD(sender_pad))
    return {};

  GstPad *receiver_pad = GST_PAD_PEER(sender_pad);
  if (GST_IS_GHOST_PAD(receiver_pad))
    return {};

  receiver_pad = get_source_pad(receiver_pad);
  sender_pad = get_source_pad(sender_pad);
  GstElement *sender_element = get_real_pad_parent(sender_pad);
  GstElement *receiver_element = GST_PAD_PARENT(receiver_pad);

  return gst_element_get_name(sender_element) + std::string(is_push ? "->" : "<-") +
         gst_element_get_name(receiver_element);
}

static void
trace_pad_push_pre(G_GNUC_UNUSED GObject *self, G_GNUC_UNUSED GstClockTime ts, GstPad *sender_pad, GstBuffer *buffer) {
  if (auto unique_name = get_unique_name(sender_pad)) {
    TracyCZone(zone, true);
    TracyCZoneName(zone, unique_name->c_str(), unique_name->length());
    TracyCPlot("GST buffer size (push)", gst_buffer_get_size(buffer));
    TracyCPlotConfig("GST buffer size (push)", TracyPlotFormatMemory, true, true, 0);
    zones[*unique_name] = zone;
  }
}

static void trace_pad_push_list_pre(G_GNUC_UNUSED GObject *self,
                                    G_GNUC_UNUSED GstClockTime ts,
                                    GstPad *sender_pad,
                                    GstBufferList *list) {
  if (auto unique_name = get_unique_name(sender_pad)) {
    TracyCZone(zone, true);
    TracyCZoneName(zone, unique_name->c_str(), unique_name->length());
    TracyCPlot("GST list buffers", gst_buffer_list_length(list));
    zones[*unique_name] = zone;
  }
}

static void
trace_pad_push_post(G_GNUC_UNUSED GObject *self, G_GNUC_UNUSED GstClockTime ts, GstPad *sender_pad, GstBuffer *buffer) {
  if (auto unique_name = get_unique_name(sender_pad)) {
    if (auto zone = zones.find(*unique_name); zone != zones.end()) {
      TracyCZoneEnd(zone->second);
      zones.erase(zone);
    } else {
      GST_WARNING("Zone not found for %s", unique_name->c_str());
    }
  }
}

static void trace_pad_pull_range_pre(
    G_GNUC_UNUSED GObject *self, G_GNUC_UNUSED GstClockTime ts, GstPad *receiver_pad, guint64 offset, guint size) {
  if (auto unique_name = get_unique_name(receiver_pad, false)) {
    TracyCZone(zone, true);
    TracyCZoneName(zone, unique_name->c_str(), unique_name->length());
    TracyCPlot("GST buffer size (pull)", size);
    TracyCPlotConfig("GST buffer size (pull)", TracyPlotFormatMemory, true, true, 0);
    zones[*unique_name] = zone;
  } else {
    GST_WARNING("Zone not found for %s", unique_name->c_str());
  }
}

static void trace_pad_push_list_post(G_GNUC_UNUSED GObject *self,
                                     G_GNUC_UNUSED GstClockTime ts,
                                     GstPad *sender_pad,
                                     GstBufferList *list) {
  if (auto unique_name = get_unique_name(sender_pad)) {
    if (auto zone = zones.find(*unique_name); zone != zones.end()) {
      TracyCZoneEnd(zone->second);
      zones.erase(zone);
    } else {
      GST_WARNING("Zone not found for %s", unique_name->c_str());
    }
  }
}

static void trace_pad_pull_range_post(G_GNUC_UNUSED GObject *self,
                                      G_GNUC_UNUSED GstClockTime ts,
                                      GstPad *receiver_pad,
                                      GstBuffer *buffer,
                                      G_GNUC_UNUSED GstFlowReturn res) {
  if (auto unique_name = get_unique_name(receiver_pad, false)) {
    if (auto zone = zones.find(*unique_name); zone != zones.end()) {
      TracyCZoneEnd(zone->second);
      zones.erase(zone);
    } else {
      GST_WARNING("Zone not found for %s", unique_name->c_str());
    }
  }
}

static void gst_tracy_tracer_init(GstTracyTracer *self) {
  GstTracer *tracer = GST_TRACER(self);

  gst_tracing_register_hook(tracer, "pad-push-pre", G_CALLBACK(trace_pad_push_pre));
  gst_tracing_register_hook(tracer, "pad-push-list-pre", G_CALLBACK(trace_pad_push_list_pre));
  gst_tracing_register_hook(tracer, "pad-push-post", G_CALLBACK(trace_pad_push_post));
  gst_tracing_register_hook(tracer, "pad-push-list-post", G_CALLBACK(trace_pad_push_list_post));
  gst_tracing_register_hook(tracer, "pad-pull-range-pre", G_CALLBACK(trace_pad_pull_range_pre));
  gst_tracing_register_hook(tracer, "pad-pull-range-post", G_CALLBACK(trace_pad_pull_range_post));
}

static gboolean gst_plugin_tracy_init(GstPlugin *plugin) {
  if (!gst_tracer_register(plugin, "tracy", GST_TYPE_TRACY_TRACER))
    return FALSE;
  return TRUE;
}

#ifndef VERSION
#define VERSION "0.1"
#endif
#ifndef PACKAGE
#define PACKAGE "gst_tracy_package"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "gst_tracy"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://FIXME.org/"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  tracy,
                  "FIXME plugin description",
                  gst_plugin_tracy_init,
                  VERSION,
                  "MIT",
                  PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN)