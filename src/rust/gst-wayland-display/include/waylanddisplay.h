#include <gstreamer-1.0/gst/gstbuffer.h>
#include <gstreamer-1.0/gst/video/video-info.h>

typedef void* WaylandDisplay;

WaylandDisplay display_init(const char *render_node);
void display_finish(WaylandDisplay dpy);

size_t display_get_devices_len(WaylandDisplay dpy);
size_t display_get_devices(WaylandDisplay dpy, const char **devices, size_t max_len);
size_t display_get_envvars_len(WaylandDisplay dpy);
size_t display_get_envvars(WaylandDisplay dpy, const char **vars, size_t max_len);

void display_add_input_device(WaylandDisplay dpy, const char *path);
void display_set_video_info(WaylandDisplay dpy, const GstVideoInfo *info);
GstBuffer *display_get_frame(WaylandDisplay dpy);
