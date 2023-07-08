#pragma once
#include <helpers/logger.hpp>
#include <map>
#include <optional>
#include <peglib.h>
#include <sstream>
#include <vector>

namespace rtsp {

enum PACKET_TYPE {
  REQUEST,
  RESPONSE
};

enum TARGET_TYPE {
  TARGET_URI,
  TARGET_STREAM
};

struct URI {
  std::string protocol;
  std::string ip; // Do not rely on this, can be missing (AndroidTV)
  unsigned short port{};
};

struct STREAM {
  std::string type;
  std::string params;
};

struct RTSP_REQUEST {
  std::string cmd;

  TARGET_TYPE type{};

  URI uri;
  STREAM stream;
};

struct RTSP_RESPONSE {
  unsigned short status_code{};
  std::string msg;
};

struct RTSP_PACKET {
  PACKET_TYPE type{};

  int seq_number{};

  RTSP_REQUEST request;
  RTSP_RESPONSE response;

  struct response;

  std::map<std::string, std::string> options = {};
  std::vector<std::pair<std::string, std::string>> payloads = {};
};

/**
 * Parse the input message; if successful will return a PACKET object.
 */
std::optional<RTSP_PACKET> parse(std::string_view msg) {

  // clang-format off
  // Test this out at https://yhirose.github.io/cpp-peglib/
  peg::parser parser = {R"(
    RTSP <- (RTSPREQUEST / RTSPRESPONSE) ENDLINE CSEQ OPTION* ENDLINE? PAYLOAD* RUBBISH?

    RTSPREQUEST <- CMD TARGET FULLPROTOCOL
    RTSPRESPONSE <- FULLPROTOCOL RESPONSECODE RESPONSEMSG

    #####
    CMD <- < [a-z]i+ >

    #####
    RESPONSECODE <- < [0-9]+ >
    RESPONSEMSG <- < [a-zA-Z ]+ >

    #####
    TARGET <- URI / STREAM / '/'
    STREAM <- [a-z]i+ '=' STREAMTYPE STREAMPARAMS

    STREAMTYPE <- < [a-z]i+  >
    STREAMPARAMS <- < [0-9/]+ >

    URI <- PROTOCOL '://' IP? ':' PORT
    PROTOCOL <- < [a-z]i+ >
    IP <-  < [0-9.]+ >
    PORT <- < [0-9]+ >

    #####
    ~FULLPROTOCOL <- FPROTOCOL '/' FVERSION
    FPROTOCOL <- < [a-z]i+ >
    FVERSION <- < [0-9.]+ >

    #####
    CSEQ <- 'CSeq:' < [0-9.]+ > ENDLINE?

    #####
    OPTION <- OPTKEY ':' OPTVAL ENDLINE?
    OPTKEY <- < [a-zA-Z0-9-_]+ >
    OPTVAL <- < [a-zA-Z0-9-_./;=, :]+ > / ''

    #####
    PAYLOAD <- PAYLOADKEY '=' PAYLOADVAL ENDLINE?
    PAYLOADKEY <- < [a-zA-Z0-9-_]+ >
    PAYLOADVAL <- < [a-zA-Z0-9-_./[\]\\:  =]+ > / ''

    #####
    CR <- '\r'
    LF <- '\n'
    ~ENDLINE <- CR? LF
    RUBBISH <- < .+ > # Any unicode character

    %whitespace  <-  [ \t]*
    )"};
  // clang-format on

  if (!static_cast<bool>(parser)) { // If this fails we have passed a bad grammar
    return {};
  }

  RTSP_PACKET pkt;

  parser["RTSPREQUEST"] = [&pkt](const peg::SemanticValues &vs) { pkt.type = REQUEST; };
  parser["RTSPRESPONSE"] = [&pkt](const peg::SemanticValues &vs) { pkt.type = RESPONSE; };

  parser["RESPONSECODE"] = [&pkt](const peg::SemanticValues &vs) {
    pkt.response.status_code = vs.token_to_number<unsigned short>();
  };
  parser["RESPONSEMSG"] = [&pkt](const peg::SemanticValues &vs) { pkt.response.msg = vs.token(); };

  parser["CMD"] = [&pkt](const peg::SemanticValues &vs) { pkt.request.cmd = vs.token(); };
  parser["CSEQ"] = [&pkt](const peg::SemanticValues &vs) { pkt.seq_number = vs.token_to_number<int>(); };

  // Target = STREAM
  parser["STREAM"] = [&pkt](const peg::SemanticValues &vs) { pkt.request.type = TARGET_STREAM; };
  parser["STREAMTYPE"] = [&pkt](const peg::SemanticValues &vs) { pkt.request.stream.type = vs.token(); };
  parser["STREAMPARAMS"] = [&pkt](const peg::SemanticValues &vs) { pkt.request.stream.params = vs.token(); };

  // Target = URI
  parser["URI"] = [&pkt](const peg::SemanticValues &vs) { pkt.request.type = TARGET_URI; };
  parser["PROTOCOL"] = [&pkt](const peg::SemanticValues &vs) { pkt.request.uri.protocol = vs.token(); };
  parser["IP"] = [&pkt](const peg::SemanticValues &vs) { pkt.request.uri.ip = vs.token(); };
  parser["PORT"] = [&pkt](const peg::SemanticValues &vs) {
    pkt.request.uri.port = vs.token_to_number<unsigned short>();
  };

  // Options map
  parser["OPTION"] = [&pkt](const peg::SemanticValues &vs) {
    auto key = std::any_cast<std::string>(vs[0]);
    auto val = std::any_cast<std::string>(vs[1]);
    pkt.options[key] = val;
  };
  parser["OPTKEY"] = [](const peg::SemanticValues &vs) { return vs.token_to_string(); };
  parser["OPTVAL"] = [](const peg::SemanticValues &vs) { return vs.token_to_string(); };

  // Payloads map
  parser["PAYLOAD"] = [&pkt](const peg::SemanticValues &vs) {
    auto key = std::any_cast<std::string>(vs[0]);
    auto val = std::any_cast<std::string>(vs[1]);
    pkt.payloads.emplace_back(key, val);
  };
  parser["PAYLOADKEY"] = [](const peg::SemanticValues &vs) { return vs.token_to_string(); };
  parser["PAYLOADVAL"] = [](const peg::SemanticValues &vs) { return vs.token_to_string(); };

  parser.set_logger([msg](size_t line, size_t col, const std::string &error_msg, const std::string &rule) {
    logs::log(logs::warning, "RTSP - {}:{}: {}\n{}", line, col, error_msg, msg);
  });

  parser.enable_packrat_parsing();
  if (!parser.parse(msg)) { // If this fails we have passed a packet that doesn't conform to the grammar
    return {};
  }
  return pkt;
}

/**
 * Turns the packet into a string, ready to be fired down the UDP channel
 */
std::string to_string(const RTSP_PACKET &pkt) {
  std::ostringstream stream;
  constexpr auto endl = "\r\n";

  if (pkt.type == REQUEST) {
    stream << pkt.request.cmd << " ";

    if (pkt.request.type == TARGET_URI) {
      stream << pkt.request.uri.protocol << "://" << pkt.request.uri.ip << ":" << pkt.request.uri.port;
    } else {
      stream << "streamid=" << pkt.request.stream.type << pkt.request.stream.params;
    }

    stream << " RTSP/1.0";
  } else {
    stream << "RTSP/1.0 " << pkt.response.status_code << " " << pkt.response.msg;
  }

  stream << endl << "CSeq: " << pkt.seq_number;

  for (const auto &opt : pkt.options) {
    stream << "\r\n" << opt.first << ": " << opt.second;
  }
  stream << endl << endl;
  for (const auto &pay : pkt.payloads) {
    if (pay.first.empty()) {
      stream << pay.second << endl;
    } else {
      stream << pay.first << "=" << pay.second << endl;
    }
  }

  return stream.str();
}

} // namespace rtsp
