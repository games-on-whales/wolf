#include <fmt/core.h>
#include <fmt/format.h>
#include <gst/gst.h>
#include <string>

namespace streaming {

/**
 * GStreamer needs to be initialised once per run
 * Call this method in your main.
 *
 * It is also possible to call the init function with two NULL arguments,
 * in which case no command line options will be parsed by GStreamer.
 */
void init(int argc, char *argv[]) {
  gst_init(&argc, &argv);
}

/**
 * @return the Gstreamer version we are linked to
 */
std::string version() {
  guint major, minor, micro, nano;
  gst_version(&major, &minor, &micro, &nano);
  return fmt::format("{}.{}.{}-{}", major, minor, micro, nano);
}

void play(const std::string &source_name, const std::string &sink_name) {
  GMainLoop *loop;

  GstElement *pipeline, *source, *filter, *sink;
  GstBus *bus;
  guint bus_watch_id;

  loop = g_main_loop_new(nullptr, FALSE);

  pipeline = gst_pipeline_new("videotest-pipeline");
  source = gst_element_factory_make(source_name.c_str(), "source");
  sink = gst_element_factory_make(sink_name.c_str(), "sink");

  if (!pipeline || !source || !sink) {
    g_printerr("One element could not be created. Exiting.\n");
    return;
  }

  /* we add all elements into the pipeline */
  gst_bin_add_many(GST_BIN(pipeline), source, sink, NULL);

  /* we link the elements together */
  gst_element_link(source, sink);

  /* Set the pipeline to "playing" state*/
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  /* Iterate */
  g_main_loop_run(loop);

  /* Out of the main loop, clean up nicely */
  gst_element_set_state(pipeline, GST_STATE_NULL);

  gst_object_unref(GST_OBJECT(pipeline));
  g_main_loop_unref(loop);
}

} // namespace streaming