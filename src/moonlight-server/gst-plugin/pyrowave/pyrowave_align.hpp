// SPDX-License-Identifier: GPL-3.0-or-later
//
// Payload-boundary alignment for the PyroWave bitstream.
//
// PyroWave packs a frame into a sequence of self-describing records (a frame header followed by one
// record per coded wavelet block). The RTP layer splays that byte stream over fixed-size payloads.
// A record is only resynchronisable if it starts exactly at a payload boundary, so the encoder is
// expected to insert in-band padding records whenever a record that would fit inside a single
// payload does not start at one.
//
// The padding record is understood by PyroWave decoders derived from the WiVRn fork, which is what
// the Moonlight clients (Aurora) carry:
//
//   u32 magic (0xFFFFFFFF)
//   u32 word count N
//   N zero u32 words
//
// A parser reading a record header sees the magic and skips `8 + 4 * N` bytes. Padding is only ever
// inserted between records, never inside one, so a record that is larger than a payload is left to
// span payloads untouched.
//
// This header is deliberately free of any pyrowave/GStreamer dependency so it can be unit tested
// everywhere.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wolf::gst_pyrowave {

/**
 * One encoder-produced PyroWave record inside the packetizer's scratch buffer.
 *
 * Records are expected to be non-overlapping and in emission order, and both their offset and their
 * size must be multiples of 4 (the PyroWave bitstream is made of 32-bit words, and every record
 * starts on a word boundary).
 *
 * The entry points below are templates over the record type so that the packetizer's own
 * pyrowave_packet objects can be laid out in place; any type exposing `offset` and `size` works.
 */
struct PacketSpan {
  std::size_t offset; /**< Byte offset of the record inside the source buffer. */
  std::size_t size;   /**< Size of the record in bytes. */
};

/** Magic of the in-band PyroWave padding record; see the file header comment. */
constexpr std::uint32_t BITSTREAM_PADDING_MAGIC = 0xFFFFFFFFu;

namespace detail {

/**
 * Counts, without writing, the bytes a sink would receive.
 *
 * @see align_impl
 */
struct CountingSink {
  std::size_t bytes = 0;

  /** @return Total size of the stream produced so far. */
  std::size_t total() const noexcept {
    return bytes;
  }

  /** Accounts for `len` bytes copied from the source. */
  void copy_from_src(std::size_t, std::size_t len) noexcept {
    bytes += len;
  }

  /** Accounts for a `len` byte padding record. */
  void append_padding(std::size_t len) noexcept {
    bytes += len;
  }
};

/**
 * Writes the aligned stream into a caller-provided buffer.
 *
 * @see align_impl
 */
struct WritingSink {
  std::uint8_t *dst;       /**< Destination buffer, at least `aligned_size` bytes. */
  const std::uint8_t *src; /**< Source buffer the `PacketSpan` offsets refer to. */
  std::size_t written = 0;

  /** @return Total size of the stream produced so far. */
  std::size_t total() const noexcept {
    return written;
  }

  /**
   * Copies one record from the source buffer to the output.
   *
   * @param offset Byte offset of the record in `src`.
   * @param len Size of the record in bytes.
   */
  void copy_from_src(std::size_t offset, std::size_t len) noexcept {
    if (len == 0) {
      return;
    }
    std::memcpy(dst + written, src + offset, len);
    written += len;
  }

  /**
   * Appends one padding record of exactly `len` bytes.
   *
   * @param len Total record size; must be a multiple of 4 and at least 8.
   */
  void append_padding(std::size_t len) noexcept {
    const std::uint32_t magic = BITSTREAM_PADDING_MAGIC;
    const auto word_count = static_cast<std::uint32_t>(len / 4 - 2);
    std::memcpy(dst + written, &magic, sizeof(magic));
    std::memcpy(dst + written + sizeof(magic), &word_count, sizeof(word_count));
    std::memset(dst + written + 2 * sizeof(std::uint32_t), 0, len - 2 * sizeof(std::uint32_t));
    written += len;
  }
};

/**
 * Walks the records and drives `sink` with the aligned byte stream.
 *
 * Alignment walks a monotonically increasing boundary through the *output* stream: `boundary` is the
 * offset at which the next payload starts. Whenever a record would start close to but not exactly on
 * a boundary, the gap is filled with a padding record, which pushes the record onto the following
 * boundary. A record too large for one payload is emitted as-is and simply spans payloads; the
 * boundary then catches up on the next iteration.
 *
 * A gap of exactly 4 bytes cannot hold a padding record (the record has an 8-byte header), so it is
 * filled together with the following payload: one payload is burned on padding. That is deliberate,
 * costs ~0.3% of a 1392-byte payload and only happens for the rare 4-byte tail.
 *
 * @param packets Records to lay out, in order.
 * @param count Number of records.
 * @param shard RTP payload size in bytes; 0 (or a value too small for the RTP prefix, or one that is
 * not a multiple of 4) disables alignment and concatenates the records.
 * @param first_offset Bytes the RTP layer prepends to the first payload (the video short header).
 * @param sink Receives the output, either counting or writing it.
 * @return Total size of the aligned stream in bytes.
 */
template <typename Packet, typename Sink>
std::size_t
align_impl(const Packet *packets, std::size_t count, std::size_t shard, std::size_t first_offset, Sink &sink) noexcept {
  if (shard <= first_offset || shard % 4 != 0 || first_offset % 4 != 0) {
    // No alignment: records may freely span payload boundaries. This also covers the degenerate
    // cases where padding records (which are made of 32-bit words) could not be laid out.
    for (std::size_t i = 0; i < count; i++) {
      sink.copy_from_src(packets[i].offset, packets[i].size);
    }
    return sink.total();
  }

  std::size_t pos = 0;                         // Bytes of PyroWave bitstream emitted so far.
  std::size_t boundary = shard - first_offset; // Start of the second payload in output offsets.

  for (std::size_t i = 0; i < count; i++) {
    const auto &packet = packets[i];

    while (pos > boundary) {
      // A previous over-long record spanned this payload; move to the next boundary.
      boundary += shard;
    }
    if (pos == boundary) {
      // Already aligned; consume this payload's boundary.
      boundary += shard;
    }

    const std::size_t gap = boundary - pos;
    if (packet.size <= shard && packet.size > gap) {
      if (gap >= 2 * sizeof(std::uint32_t)) {
        sink.append_padding(gap);
        pos = boundary;
        boundary += shard;
      } else {
        sink.append_padding(gap + shard);
        pos += gap + shard;
        boundary += 2 * shard;
      }
    }

    sink.copy_from_src(packet.offset, packet.size);
    pos += packet.size;
  }

  return sink.total();
}

} // namespace detail

/**
 * Computes the exact size of the aligned byte stream for `packets`.
 *
 * @tparam Packet Record type, exposing `offset` and `size`.
 * @param packets Records to lay out, in order.
 * @param count Number of records.
 * @param shard RTP payload size in bytes; 0 disables alignment.
 * @param first_offset Bytes the RTP layer prepends to the first payload.
 * @return Size in bytes that `write_aligned` will produce.
 */
template <typename Packet>
std::size_t
aligned_size(const Packet *packets, std::size_t count, std::size_t shard, std::size_t first_offset) noexcept {
  detail::CountingSink sink;
  return detail::align_impl(packets, count, shard, first_offset, sink);
}

/**
 * Copies `packets` out of `src` into `dst`, inserting padding records so that every record that fits
 * inside one RTP payload starts at a payload boundary.
 *
 * @tparam Packet Record type, exposing `offset` and `size`.
 * @param dst Destination buffer, must be at least `aligned_size(packets, count, shard, first_offset)`
 * bytes.
 * @param dst_size Size of `dst` in bytes.
 * @param src Source buffer that the `PacketSpan` offsets refer to.
 * @param packets Records to lay out, in order.
 * @param count Number of records.
 * @param shard RTP payload size in bytes; 0 disables alignment.
 * @param first_offset Bytes the RTP layer prepends to the first payload.
 * @return Number of bytes written, or 0 if `dst_size` is too small (in which case `dst` is
 * untouched).
 */
template <typename Packet>
std::size_t write_aligned(std::uint8_t *dst,
                          std::size_t dst_size,
                          const std::uint8_t *src,
                          const Packet *packets,
                          std::size_t count,
                          std::size_t shard,
                          std::size_t first_offset) noexcept {
  if (aligned_size(packets, count, shard, first_offset) > dst_size) {
    return 0;
  }

  detail::WritingSink sink{dst, src};
  return detail::align_impl(packets, count, shard, first_offset, sink);
}

} // namespace wolf::gst_pyrowave
