#pragma once
#include <string>
#include <cstdint>

namespace moonlight {

struct DisplayMode {
  int width;
  int height;
  int refreshRate;
  bool hevc_supported = true;
};

struct App {
  const std::string title;
  const std::string id;
  const bool support_hdr;
};

#define FLAG_EXTENSION 0x10

#define FLAG_CONTAINS_PIC_DATA 0x1
#define FLAG_EOF 0x2
#define FLAG_SOF 0x4

#define MAX_RTP_HEADER_SIZE 16

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