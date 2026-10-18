#pragma once

#include <chrono>
#include <core/gstreamer.hpp>
#include <core/virtual-display.hpp>
#include <future>
#include <thread>

namespace wolf::core::virtual_display {

using namespace wolf::core::gstreamer;

class TestWaylandDisplay {
public:
  gst_element_ptr pipeline;
  gst_element_ptr wayland_plugin;
  wl_state_ptr w_state;

  TestWaylandDisplay(const DisplayMode &mode) {
    auto pipeline_str = std::string("waylanddisplaysrc name=wolf_wayland_source render-node=software ! ") +
                        "video/x-raw" +                                  //
                        ", width=" + std::to_string(mode.width) +        //
                        ", height=" + std::to_string(mode.height) +      //
                        ", framerate=" + std::to_string(mode.refreshRate) + "/1" + //
                        " ! fakesink";

    GError *error = nullptr;
    pipeline = gstreamer::gst_element_ptr(gst_parse_launch(pipeline_str.c_str(), &error), ::gst_object_unref);
    if (error) {
      auto msg = std::string(error->message);
      g_error_free(error);
      throw std::runtime_error("Pipeline parse error: " + msg);
    }
    if (!pipeline) {
      throw std::runtime_error("Failed to create pipeline");
    }

    auto el = gst_bin_get_by_name(GST_BIN(pipeline.get()), "wolf_wayland_source");
    wayland_plugin = gstreamer::gst_element_ptr(el, ::gst_object_unref);

    auto socket_promise = std::make_shared<std::promise<std::string>>();
    auto socket_future = socket_promise->get_future();
    main_loop_ = std::make_shared<gstreamer::gst_main_loop_ptr>();

    auto captured_pipeline = pipeline;
    auto captured_loop = main_loop_;

    pipeline_thread_ = std::thread([captured_pipeline, socket_promise, captured_loop]() {
      gstreamer::gst_main_context_ptr context(g_main_context_new(), ::g_main_context_unref);
      g_main_context_push_thread_default(context.get());
      *captured_loop = gstreamer::gst_main_loop_ptr(g_main_loop_new(context.get(), FALSE), ::g_main_loop_unref);

      auto bus = gst_pipeline_get_bus(GST_PIPELINE(captured_pipeline.get()));
      gst_bus_add_signal_watch(bus);

      g_signal_connect(
          bus, "message::application",
          G_CALLBACK(+[](GstBus *, GstMessage *msg, gpointer data) {
            auto p = static_cast<std::promise<std::string> *>(data);
            auto structure = gst_message_get_structure(msg);
            if (gst_structure_has_name(structure, "wayland.src")) {
              if (auto display = gst_structure_get_string(structure, "WAYLAND_DISPLAY")) {
                p->set_value(display);
              }
            }
          }),
          socket_promise.get());

      g_signal_connect(
          bus, "message::error", G_CALLBACK(gstreamer::pipeline_error_handler), captured_loop->get());
      g_signal_connect(bus, "message::eos", G_CALLBACK(gstreamer::pipeline_eos_handler), captured_loop->get());
      gst_object_unref(bus);

      gst_element_set_state(captured_pipeline.get(), GST_STATE_PLAYING);
      g_main_loop_run(captured_loop->get());

      gst_element_set_state(captured_pipeline.get(), GST_STATE_PAUSED);
      gst_element_set_state(captured_pipeline.get(), GST_STATE_READY);
      gst_element_set_state(captured_pipeline.get(), GST_STATE_NULL);
    });

    if (socket_future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
      if (main_loop_ && *main_loop_)
        g_main_loop_quit(main_loop_->get());
      if (pipeline_thread_.joinable())
        pipeline_thread_.join();
      throw std::runtime_error("Timed out waiting for wayland.src socket message");
    }

    w_state = create_wayland_display(wayland_plugin, socket_future.get());
  }

  ~TestWaylandDisplay() {
    if (main_loop_ && *main_loop_)
      g_main_loop_quit(main_loop_->get());
    if (pipeline_thread_.joinable())
      pipeline_thread_.join();
  }

  TestWaylandDisplay(const TestWaylandDisplay &) = delete;
  TestWaylandDisplay &operator=(const TestWaylandDisplay &) = delete;

private:
  std::shared_ptr<gstreamer::gst_main_loop_ptr> main_loop_;
  std::thread pipeline_thread_;
};

} // namespace wolf::core::virtual_display
