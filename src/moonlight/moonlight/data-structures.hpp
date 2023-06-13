#pragma once
#include <string>

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

namespace control {

enum PACKET_TYPE {
  START_A = 0x0305,
  START_B = 0x0307,
  INVALIDATE_REF_FRAMES = 0x0301,
  LOSS_STATS = 0x0201,
  FRAME_STATS = 0x0204,
  INPUT_DATA = 0x0206,
  RUMBLE_DATA = 0x010b,
  TERMINATION = 0x0100,
  PERIODIC_PING = 0x0200,
  IDR_FRAME = 0x0302,
  ENCRYPTED = 0x0001
};

/**
 * Events received in the ControlSession will be fired up in the event_bus
 */
struct ControlEvent {
  // A unique ID that identifies this session
  std::size_t session_id;

  PACKET_TYPE type;
  std::string_view raw_packet;
};

} // namespace control

struct PauseStreamEvent {
  std::size_t session_id;
};

struct ResumeStreamEvent {
  std::size_t session_id;
};

struct StopStreamEvent {
  std::size_t session_id;
};

} // namespace moonlight