## Standalone Gstreamer plugins

### Tracy

A Gstreamer tracer plugin based on [wolfpld/tracy](https://github.com/wolfpld/tracy)

Build the plugin:

``` 
mkdir build
cd build
cmake ..
cmake --build .
```

Example usage:

``` 
GST_DEBUG=2,GST_TRACER:7 GST_TRACERS=tracy GST_PLUGIN_PATH=${PWD} gst-launch-1.0 videotestsrc ! cudaupload ! nvh264enc ! openh264dec ! autovideosink
```

Install and connect the Tracy client to the server where the pipeline is running.
(Note: running `gst-launch-1.0` with a privileged user will show additional information in Tracy).

### rtpmoonlightpay_video

### rtpmoonlightpay_audio