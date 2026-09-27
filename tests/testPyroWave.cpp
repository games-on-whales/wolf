// Tests for the PyroWave bitstream aligner.
//
// The aligner is the part of the PyroWave support that decides whether the client's depacketizer can
// resynchronise after a lost RTP packet, so these tests check the wire property directly: no payload
// boundary may fall inside a record that would have fitted in a single payload, every padding record
// must be well formed, and the records must survive the padding untouched.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <gst-plugin/pyrowave/pyrowave_align.hpp>
#include <random>
#include <vector>

#ifdef WOLF_PYROWAVE
#include <gst-plugin/pyrowave/gstpyrowaveenc.hpp>
#endif

using namespace wolf::gst_pyrowave;

namespace {

/** Bytes the RTP layer prepends to the first payload, see gst-plugin/video.hpp. */
constexpr std::size_t FIRST_OFFSET = 8;

/** Statistics gathered while walking an aligned stream. */
struct StreamStats {
  std::size_t total = 0;
  std::size_t padding_records = 0;
  std::size_t padding_bytes = 0;
};

/**
 * Aligns a synthetic frame made of `sizes` records and checks every property the client relies on.
 *
 * @param shard RTP payload size in bytes, 0 to disable alignment.
 * @param sizes Size of each record, in emission order.
 * @param expected_total Expected aligned size, or -1 to skip the check.
 * @return Statistics about the produced stream.
 */
StreamStats check_alignment(std::size_t shard, const std::vector<std::size_t> &sizes, long expected_total = -1) {
  std::vector<std::uint8_t> source;
  std::vector<PacketSpan> packets;
  for (std::size_t size : sizes) {
    packets.push_back({source.size(), size});
    for (std::size_t i = 0; i < size; i++) {
      source.push_back(static_cast<std::uint8_t>((source.size() * 31 + 7) & 0xFF));
    }
  }

  const std::size_t needed = aligned_size(packets.data(), packets.size(), shard, FIRST_OFFSET);
  if (expected_total >= 0) {
    REQUIRE(needed == static_cast<std::size_t>(expected_total));
  }

  // write_aligned must fill the buffer it was given and stay inside it.
  std::vector<std::uint8_t> aligned(needed + 64, 0xCD);
  const auto written =
      write_aligned(aligned.data(), needed, source.data(), packets.data(), packets.size(), shard, FIRST_OFFSET);
  REQUIRE(written == needed);
  for (std::size_t i = needed; i < aligned.size(); i++) {
    REQUIRE(aligned[i] == 0xCD);
  }
  aligned.resize(needed);

  // A destination that is one byte short must be refused without touching it.
  if (needed > 0) {
    std::vector<std::uint8_t> too_small(needed + 8, 0xCD);
    REQUIRE(write_aligned(too_small.data(),
                          needed - 1,
                          source.data(),
                          packets.data(),
                          packets.size(),
                          shard,
                          FIRST_OFFSET) == 0);
    REQUIRE(std::all_of(too_small.begin(), too_small.end(), [](std::uint8_t byte) { return byte == 0xCD; }));
  }

  StreamStats stats;
  stats.total = needed;

  // Walk the stream back: padding records must carry the fork's magic and be zero filled, and the
  // records themselves must come out byte for byte.
  std::size_t position = 0;
  std::size_t index = 0;
  while (position < aligned.size()) {
    std::uint32_t magic = 0, words = 0;
    if (aligned.size() - position >= 8) {
      std::memcpy(&magic, &aligned[position], sizeof(magic));
      std::memcpy(&words, &aligned[position + sizeof(magic)], sizeof(words));
    }

    if (magic == BITSTREAM_PADDING_MAGIC) {
      const std::size_t length = 8 + 4 * static_cast<std::size_t>(words);
      REQUIRE(length % 4 == 0);
      REQUIRE(aligned.size() - position >= length);
      for (std::size_t i = 8; i < length; i++) {
        REQUIRE(aligned[position + i] == 0);
      }
      stats.padding_records++;
      stats.padding_bytes += length;
      position += length;
      continue;
    }

    REQUIRE(index < sizes.size());
    const std::size_t length = sizes[index];
    REQUIRE(aligned.size() - position >= length);
    REQUIRE(std::memcmp(&aligned[position], &source[packets[index].offset], length) == 0);

    if (shard > FIRST_OFFSET && length <= shard) {
      // A payload boundary inside a record would desynchronise the client's parser.
      for (std::size_t boundary = shard - FIRST_OFFSET; boundary < aligned.size(); boundary += shard) {
        REQUIRE(!(boundary > position && boundary < position + length));
      }
    }

    position += length;
    index++;
  }
  REQUIRE(index == sizes.size());

  // The parser recovers the records by skipping padding records, and the RTP layer's payload split
  // does not reorder anything.
  if (shard > FIRST_OFFSET) {
    std::vector<std::uint8_t> flat;
    std::size_t first = std::min(shard - FIRST_OFFSET, aligned.size());
    flat.insert(flat.end(), aligned.begin(), aligned.begin() + first);
    std::size_t cursor = first;
    while (cursor < aligned.size()) {
      const std::size_t chunk = std::min(shard, aligned.size() - cursor);
      flat.insert(flat.end(), aligned.begin() + cursor, aligned.begin() + cursor + chunk);
      cursor += chunk;
    }
    REQUIRE(flat == aligned);

    std::size_t read = 0;
    std::size_t recovered = 0;
    while (read < flat.size()) {
      std::uint32_t magic = 0, words = 0;
      if (flat.size() - read >= 8) {
        std::memcpy(&magic, &flat[read], sizeof(magic));
        std::memcpy(&words, &flat[read + sizeof(magic)], sizeof(words));
      }
      if (magic == BITSTREAM_PADDING_MAGIC) {
        read += 8 + 4 * static_cast<std::size_t>(words);
        continue;
      }

      REQUIRE(recovered < sizes.size());
      const std::size_t length = sizes[recovered];
      REQUIRE(flat.size() - read >= length);
      REQUIRE(std::memcmp(&flat[read], &source[packets[recovered].offset], length) == 0);
      read += length;
      recovered++;
    }
    REQUIRE(recovered == sizes.size());
  }

  return stats;
}

} // namespace

TEST_CASE("PyroWave alignment", "[PyroWave]") {
  SECTION("records that already land on payload boundaries need no padding") {
    // 8 + 1360 = 1368 = the first payload boundary.
    auto stats = check_alignment(1376, {8, 1360, 8}, 1376);
    REQUIRE(stats.padding_records == 0);
  }

  SECTION("a record that would cross a boundary is pushed onto the next one") {
    // 8 + 500 = 508, the 500 byte record that follows does not fit in the 492 bytes left.
    auto stats = check_alignment(1008, {8, 500, 500, 500}, 2000);
    REQUIRE(stats.padding_records == 1);
    REQUIRE(stats.padding_bytes == 492);
  }

  SECTION("a record larger than a payload is left to span payloads") {
    // 1360 + 1372 = 2740 crosses the boundary at 1376 without any padding.
    auto stats = check_alignment(1376, {8, 1360, 1372, 4, 16380}, 19124);
    REQUIRE(stats.padding_records == 0);
  }

  SECTION("a 4 byte tail burns one payload instead of leaving a boundary unusable") {
    auto stats = check_alignment(1008, {8, 988, 8}, 2016);
    REQUIRE(stats.padding_records == 1);
    REQUIRE(stats.padding_bytes == 1012);
  }

  SECTION("an unavailable alignment falls back to a plain concatenation") {
    auto stats = check_alignment(0, {8, 100, 200, 3}, 311);
    REQUIRE(stats.padding_records == 0);

    // A payload that cannot hold a padding record is not aligned either.
    stats = check_alignment(4, {8, 100}, 108);
    REQUIRE(stats.padding_records == 0);

    stats = check_alignment(1008, {}, 0);
    REQUIRE(stats.total == 0);
  }

  SECTION("randomized layouts keep every record inside a single payload") {
    std::mt19937 random(12345);
    for (int iteration = 0; iteration < 256; iteration++) {
      const std::size_t shard = 4 * (64 + (random() % 400));
      const std::size_t count = random() % 24;
      std::vector<std::size_t> sizes;
      for (std::size_t i = 0; i < count; i++) {
        const std::size_t kind = random() % 100;
        if (kind < 60) {
          sizes.push_back(4 * (1 + random() % 200));
        } else if (kind < 80) {
          sizes.push_back(4 * (1 + random() % 5));
        } else {
          // Deliberately larger than a payload every now and then.
          sizes.push_back(4 * (200 + random() % (8 * shard / 4)));
        }
      }
      check_alignment(shard, sizes);
    }
  }
}

#ifdef WOLF_PYROWAVE

/**
 * The bitrate property must accept every value the RTSP layer can derive from a client's request.
 *
 * High frame rates push the negotiated bitrate past the old 500000 kbit/s ceiling, and GLib then
 * refuses `g_object_set_property` outright: the property silently stays at its 15500 default and the
 * encoder's per-frame budget collapses to the 16 KiB floor, which is what made 120/240 FPS look
 * like 240p. 863308 kbit/s is what Wolf derives from a 1080 Mbit/s request at 5120x1440@240.
 */
TEST_CASE("PyroWave encoder accepts high-FPS bitrates", "[PyroWave]") {
  auto *element = GST_ELEMENT(g_object_new(gst_TYPE_pyrowave_enc, nullptr));
  REQUIRE(element != nullptr);
  gst_object_ref_sink(element);

  g_object_set(element, "bitrate", 863308, nullptr);

  gint bitrate = 0;
  g_object_get(element, "bitrate", &bitrate, nullptr);
  REQUIRE(bitrate == 863308);

  gst_object_unref(element);
}

#endif
