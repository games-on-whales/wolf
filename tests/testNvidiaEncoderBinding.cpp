#include <catch2/catch_test_macros.hpp>
#include <streaming/streaming.hpp>

TEST_CASE("NVIDIA encoder factories are bound per session", "[NVIDIA][Streaming]") {
  const std::string pipeline = "interpipesrc ! cudaupload ! cudaconvertscale ! nvh265enc bitrate=10000 ! appsink";

  REQUIRE(streaming::bind_nvidia_encoder(pipeline, 0) == pipeline);
  REQUIRE(streaming::bind_nvidia_encoder(pipeline, 1).find("nvh265device1enc bitrate=10000") != std::string::npos);
  REQUIRE(streaming::bind_nvidia_encoder(pipeline, 1).find("nvh265enc bitrate") == std::string::npos);
  REQUIRE(streaming::bind_nvidia_encoder("nvh264enc ! nvav1enc ! nvh265device0enc", 1) ==
          "nvh264device1enc ! nvav1device1enc ! nvh265device0enc");
}
