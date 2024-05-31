#ifndef WOLF_GSTTRACY_HPP
#define WOLF_GSTTRACY_HPP

#include <gst/gst.h>
#include <gst/gsttracer.h>

G_BEGIN_DECLS

#define GST_TYPE_TRACY_TRACER (gst_tracy_tracer_get_type ())
G_DECLARE_DERIVABLE_TYPE (GstTracyTracer, gst_tracy_tracer, GST_TRACY, TRACER, GstTracer)

struct _GstTracyTracerClass {
  GstTracerClass parent_class;
};

GType gst_tracy_tracer_get_type(void);

G_END_DECLS

#endif // WOLF_GSTTRACY_HPP
