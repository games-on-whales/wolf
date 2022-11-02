#pragma once
#include <helpers/logger.hpp>
#include <memory>
#include <moonlight/data-structures.hpp>
#include <vector>

extern "C" {
#include <rtsp/rtsp.h>
}

namespace rtsp {

void free_msg(PRTSP_MESSAGE msg) {
  freeMessage(msg);
  delete msg;
}

using msg_t = std::unique_ptr<RTSP_MESSAGE, decltype(&free_msg)>;

struct RTSP_MESSAGE_OPTION {
  const std::string &option;
  const std::string &content;
  char flags = 0; // seems to be always 0 on Moonlight/RtspParser.c
};

/**
 * It'll copy the content of the string_view into a c-style char*
 * @note: Use carefully, you'll have to free the memory!!!
 *
 * This is useful for the times where the original string could go out of scope and we'll result in a dangling pointer
 */
char *c_str_copy(std::string_view str) {
  auto size = str.size();
  auto ptr = (char *)malloc(size);
  ptr[str.copy(ptr, size)] = 0; // copy and make sure it's null terminated
  return ptr;
}

/**
 * OPTION_ITEM on Moonlight is a hidden linked list
 * listify turns a vector of RTSP_MESSAGE_OPTION into a linked lists of OPTION_ITEMS and then returns the head
 */
POPTION_ITEM listify(const std::vector<RTSP_MESSAGE_OPTION> &options) {
  POPTION_ITEM head_opt = nullptr;
  POPTION_ITEM *current_opt = &head_opt;
  for (const auto &option : options) {
    *current_opt = new OPTION_ITEM{option.flags, c_str_copy(option.option), c_str_copy(option.content), *current_opt};
    current_opt = &(*current_opt)->next;
  }
  return head_opt;
}

msg_t create_rtsp_msg(const std::vector<RTSP_MESSAGE_OPTION> &options,
                      int statuscode,
                      const char *status_msg,
                      int seqn,
                      std::string_view payload) {
  msg_t resp(new RTSP_MESSAGE, free_msg);

  char *c_payload = nullptr;
  int payload_size = 0;
  if (!payload.empty()) {
    c_payload = c_str_copy(payload);
    payload_size = (int)payload.size();
  }

  createRtspResponse(resp.get(),
                     nullptr,
                     0,
                     const_cast<char *>("RTSP/1.0"),
                     statuscode,
                     const_cast<char *>(status_msg),
                     seqn,
                     listify(options),
                     c_payload,
                     payload_size);
  return resp;
}

msg_t create_error_msg(int status_code, std::string_view error_msg, int sequence_number = 0) {
  return create_rtsp_msg({{"CSeq", std::to_string(sequence_number)}},
                         status_code,
                         error_msg.data(),
                         sequence_number,
                         {});
}

std::optional<msg_t> parse_rtsp_msg(std::string_view msg, int length) {
  msg_t parsed(new RTSP_MESSAGE, free_msg);
  if (int result_code = parseRtspMessage(parsed.get(), const_cast<char *>(msg.data()), length) != RTSP_ERROR_SUCCESS) {
    logs::log(logs::error, "[RTSP] Unable to parse message, code: {}", result_code);
    return {};
  } else {
    return parsed;
  }
}

std::string serialize_rtsp_msg(msg_t msg) {
  int serialized_len;
  std::unique_ptr<char, decltype(&free)> serialized = {serializeRtspMessage(msg.get(), &serialized_len), free};
  return {serialized.get(), (size_t)serialized_len};
}

} // namespace rtsp