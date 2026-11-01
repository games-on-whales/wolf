#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace moonlight {

struct DisplayMode {
  int width;
  int height;
  int refreshRate;
  bool hevc_supported = true;
  bool av1_supported = false;
};

struct App {
  const std::string title;
  const std::string id;
  const bool support_hdr;
  std::optional<std::string> icon_png_path;
};

#define FLAG_EXTENSION 0x10

#define FLAG_CONTAINS_PIC_DATA 0x1
#define FLAG_EOF 0x2
#define FLAG_SOF 0x4

#define MAX_RTP_HEADER_SIZE 16

// Client feature flags for x-ml-general.featureFlags SDP attribute
#define ML_FF_FEC_STATUS 0x01    // Client sends SS_FRAME_FEC_STATUS for frame losses
#define ML_FF_SESSION_ID_V1 0x02 // Client supports X-SS-Ping-Payload and X-SS-Connect-Data

struct SS_PING {
  std::array<char, 16> payload;
  uint32_t sequenceNumber;
};

typedef struct _NV_VIDEO_PACKET {
  uint32_t streamPacketIndex;
  uint32_t frameIndex;
  uint8_t flags;
  uint8_t reserved;
  uint8_t multiFecFlags;
  uint8_t multiFecBlocks;
  uint32_t fecInfo;
} NV_VIDEO_PACKET, *PNV_VIDEO_PACKET;

typedef struct _RTP_PACKET {
  uint8_t header;
  uint8_t packetType;
  uint16_t sequenceNumber;
  uint32_t timestamp;
  uint32_t ssrc;
} RTP_PACKET, *PRTP_PACKET;
} // namespace moonlight