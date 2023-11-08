#pragma once

#include <array>
#include <boost/endian/conversion.hpp>
#include <crypto/crypto.hpp>
#include <cstdint>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <moonlight/fec.hpp>
#include <vector>

static void gst_buffer_copy_into(GstBuffer *buf, unsigned char *destination) {
  auto size = gst_buffer_get_size(buf);
  /* get READ access to the memory and fill with vals */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_READ);
  std::copy(info.data, info.data + size, destination);
  gst_buffer_unmap(buf, &info);
}

static std::vector<unsigned char> gst_buffer_copy_content(GstBuffer *buf, unsigned long offset, unsigned long size) {
  auto vals = std::vector<unsigned char>(size);

  /* get READ access to the memory and fill with vals */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_READ);
  for (int i = 0; i < size; i++) {
    vals[i] = info.data[i + offset];
  }
  gst_buffer_unmap(buf, &info);
  return vals;
}

static std::vector<unsigned char> gst_buffer_copy_content(GstBuffer *buf, unsigned long offset) {
  return gst_buffer_copy_content(buf, offset, gst_buffer_get_size(buf) - offset);
}

static std::vector<unsigned char> gst_buffer_copy_content(GstBuffer *buf) {
  return gst_buffer_copy_content(buf, 0);
}

/**
 * Creates a GstBuffer and fill the memory with the given value
 */
static GstBuffer *gst_buffer_new_and_fill(gsize size, int fill_val) {
  GstBuffer *buf = gst_buffer_new_allocate(nullptr, size, nullptr);

  /* get WRITE access to the memory and fill with fill_val */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);
  memset(info.data, fill_val, info.size);
  gst_buffer_unmap(buf, &info);
  return buf;
}

/**
 * Creates a GstBuffer from the given array of chars
 */
static GstBuffer *gst_buffer_new_and_fill(gsize size, const char vals[]) {
  GstBuffer *buf = gst_buffer_new_allocate(nullptr, size, nullptr);
  gst_buffer_fill(buf, 0, vals, size);
  return buf;
}

/**
 * From a list of buffers returns a single buffer that contains them all.
 * No copy of the stored data is performed
 */
static GstBuffer *gst_buffer_list_unfold(GstBufferList *buffer_list) {
  GstBuffer *buf = gst_buffer_new_allocate(NULL, 0, NULL);

  for (int idx = 0; idx < gst_buffer_list_length(buffer_list); idx++) {
    auto buf_idx =
        gst_buffer_copy(gst_buffer_list_get(buffer_list, idx)); // copy here is about the buffer object, not the data
    gst_buffer_append(buf, buf_idx);
  }

  return buf;
}

/**
 * From a list of buffers returns a sub list from start (inclusive) to end (exclusive)
 * No copy of the stored data is performed
 */
static GstBufferList *gst_buffer_list_sub(GstBufferList *buffer_list, int start, int end) {
  GstBufferList *res = gst_buffer_list_new();

  for (int idx = start; idx < end; idx++) {
    auto buf_idx =
        gst_buffer_copy(gst_buffer_list_get(buffer_list, idx)); // copy here is about the buffer object, not the data
    gst_buffer_list_insert(res, -1, buf_idx);
  }

  return res;
}

/**
 * Copies out buffer metadata without affecting data
 */
static void gst_copy_timestamps(GstBuffer *src, GstBuffer *dest) {
  dest->pts = src->pts;
  dest->dts = src->dts;
  dest->offset = src->offset;
  dest->duration = src->duration;
  dest->offset_end = src->offset_end;
}

/**
 * Derives the proper IV following Moonlight implementation
 */
static std::string derive_iv(const std::string &aes_iv, int cur_seq_number) {
  auto iv = std::array<std::uint8_t, 16>{};
  std::uint32_t input_iv = std::stoul(aes_iv);
  *(std::uint32_t *)iv.data() = boost::endian::native_to_big(input_iv + cur_seq_number);
  return {iv.begin(), iv.end()};
}

/**
 * Encrypts the input buffer using AES CBC
 * @returns a new buffer with the content encrypted
 */
static GstBuffer *encrypt_payload(const std::string &aes_key, const std::string &aes_iv, GstBuffer *inbuf) {
  GstMapInfo info;
  gst_buffer_map(inbuf, &info, GST_MAP_READ);
  std::string packet_content((char *)info.data, gst_buffer_get_size(inbuf));

  auto encrypted = crypto::aes_encrypt_cbc(packet_content, aes_key, aes_iv, true);

  gst_buffer_unmap(inbuf, &info);
  return gst_buffer_new_and_fill(encrypted.size(), encrypted.c_str());
}