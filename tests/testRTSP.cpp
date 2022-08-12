#include <boost/beast/_experimental/test/stream.hpp>
#include <rtsp/rtsp.cpp>

using namespace rtsp;

/**
 * In order to test rtsp::tcp_connection we create a derived class that does the opposite:
 * instead of waiting for a message; it'll first send a message and then wait for a response.
 */
class tcp_tester : public tcp_connection {

public:
  static auto create_client(asio::io_context &io_context, int port, immer::box<StreamSession> session) {
    auto tester = new tcp_tester(io_context, std::move(session));

    tcp::resolver resolver(io_context);
    asio::connect(tester->socket(), resolver.resolve("0.0.0.0", std::to_string(port)));

    return boost::shared_ptr<tcp_tester>(tester);
  }

  void run(std::string_view raw_msg, std::function<void(std::optional<msg_t> /* response */)> on_response) {
    auto send_msg = parse_rtsp_msg(raw_msg, (int)raw_msg.size());

    send_message(std::move(send_msg.value()), [self = shared_from_this(), on_response](auto bytes) {
      self->receive_message([self = self->shared_from_this(), on_response](auto reply_msg) {
        on_response(std::move(reply_msg));
        self->socket().close();
      });
    });

    run_context();
  }

  void run_context() {
    while (socket().is_open()) {
      ioc.run_one();
    }
  }

protected:
  explicit tcp_tester(asio::io_context &io_context, immer::box<StreamSession> session)
      : tcp_connection(io_context, std::move(session)), ioc(io_context) {}

  boost::asio::io_context &ioc;
};

TEST_CASE("utilities methods", "[RTSP]") {

  SECTION("create RTSP message") {
    auto msg = create_rtsp_msg({}, 200, "OK", 1, "");
    REQUIRE(msg);
    REQUIRE(msg->sequenceNumber == 1);
    REQUIRE(msg->message.response.statusCode == 200);

    msg = create_rtsp_msg({{"CSeq", "99"}}, 200, "OK", 99, "");
    REQUIRE(msg);
    REQUIRE(msg->sequenceNumber == 99);
    REQUIRE(msg->message.response.statusCode == 200);
    REQUIRE_THAT(msg->options->option, Equals("CSeq"));
    REQUIRE_THAT(msg->options->content, Equals("99"));
  }

  SECTION("listify", "[RTSP]") {
    auto c_list = listify({{"A", "1"}, {"B", "2"}, {"C", "3"}});

    REQUIRE_THAT(c_list->option, Equals("A"));
    REQUIRE_THAT(c_list->content, Equals("1"));

    REQUIRE_THAT(c_list->next->option, Equals("B"));
    REQUIRE_THAT(c_list->next->content, Equals("2"));

    REQUIRE_THAT(c_list->next->next->option, Equals("C"));
    REQUIRE_THAT(c_list->next->next->content, Equals("3"));

    REQUIRE(c_list->next->next->next == nullptr);
  }

  SECTION("parse RTSP message", "[RTSP]") {
    auto raw_input = "OPTIONS rtsp://10.1.2.49:48010 RTSP/1.0\n"
                     "CSeq: 1\n"
                     "X-GS-ClientVersion: 14\n"
                     "Host: 10.1.2.49"
                     "\r\n\r\n"
                     "here's the payload!!"s;
    auto msg = parse_rtsp_msg(raw_input, (int)raw_input.size());

    REQUIRE(msg);
    REQUIRE_THAT(msg.value()->message.request.command, Equals("OPTIONS"));
    REQUIRE_THAT(msg.value()->message.request.target, Equals("rtsp://10.1.2.49:48010"));
    REQUIRE_THAT(msg.value()->protocol, Equals("RTSP/1.0"));

    REQUIRE(msg.value()->sequenceNumber == 1);

    auto option = msg.value()->options;
    REQUIRE_THAT(option->option, Equals("CSeq"));
    REQUIRE_THAT(option->content, Equals(" 1"));

    option = option->next;
    REQUIRE_THAT(option->option, Equals("X-GS-ClientVersion"));
    REQUIRE_THAT(option->content, Equals(" 14"));

    option = option->next;
    REQUIRE_THAT(option->option, Equals("Host"));
    REQUIRE_THAT(option->content, Equals(" 10.1.2.49"));

    option = option->next;
    REQUIRE(option == nullptr);

    REQUIRE_THAT(msg.value()->payload, Equals("here's the payload!!"));
  }
}

immer::box<StreamSession> test_init_state() {
  StreamSession session = {std::make_shared<dp::event_bus>(),
                           {1920, 1080, 60},
                           {2, 1, 1, {state::AudioMode::FRONT_LEFT, state::AudioMode::FRONT_RIGHT}},
                           "app_id_1",
                           crypto::hex_to_str("9d804e47a6aa6624b7d4b502b32cc522", true),
                           "0f691f13730748328a22a6952a5ac3a2",
                           "192.168.1.1",
                           1,
                           2,
                           3,
                           4};
  return {session};
}

TEST_CASE("Commands", "[RTSP]") {
  constexpr int port = 8080;
  boost::asio::io_context ioc;
  auto state = test_init_state();
  auto wolf_server = tcp_server(ioc, port, state);
  auto wolf_client = tcp_tester::create_client(ioc, port, state);

  SECTION("MissingNo") {
    wolf_client->run("MissingNo rtsp://10.1.2.49:48010 RTSP/1.0\r\n"
                     "CSeq: 1\r\n\r\n"sv,
                     [](std::optional<msg_t> response) {
                       REQUIRE(response);
                       REQUIRE(response.value()->message.response.statusCode == 404);
                       REQUIRE(response.value()->sequenceNumber == 1);
                     });
  }

  SECTION("OPTION") {
    wolf_client->run("OPTIONS rtsp://10.1.2.49:48010 RTSP/1.0\r\n"
                     "CSeq: 1\r\n"
                     "X-GS-ClientVersion: 14\r\n"
                     "Host: 10.1.2.49"
                     "\r\n\r\n"sv,
                     [](std::optional<msg_t> response) {
                       REQUIRE(response);
                       REQUIRE(response.value()->message.response.statusCode == 200);
                       REQUIRE(response.value()->sequenceNumber == 1);
                     });
  }

  SECTION("DESCRIBE") {
    wolf_client->run("DESCRIBE rtsp://10.1.2.49:48010 RTSP/1.0\n"
                     "CSeq: 2\n"
                     "X-GS-ClientVersion: 14\n"
                     "Host: 10.1.2.49\n"
                     "Accept: application/sdp"
                     "\r\n\r\n"sv,
                     [](std::optional<msg_t> response) {
                       REQUIRE(response);
                       REQUIRE(response.value()->message.response.statusCode == 200);
                       REQUIRE(response.value()->sequenceNumber == 2);
                       REQUIRE_THAT(response.value()->payload, Equals("\na=fmtp:97 surround-params=21101\n"));
                     });
  }

  SECTION("SETUP audio") {
    wolf_client->run("SETUP streamid=audio/0/0 RTSP/1.0\n"
                     "CSeq: 3\n"
                     "X-GS-ClientVersion: 14\n"
                     "Host: 10.1.2.49\n"
                     "Transport: unicast;X-GS-ClientPort=50000-50001\n"
                     "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT"
                     "\r\n\r\n"sv,
                     [&state](std::optional<msg_t> response) {
                       REQUIRE(response);
                       REQUIRE(response.value()->message.response.statusCode == 200);
                       REQUIRE(response.value()->sequenceNumber == 3);

                       auto opt = response.value()->options->next;
                       REQUIRE_THAT(opt->option, Equals("Session"));
                       REQUIRE_THAT(opt->content, Equals(" DEADBEEFCAFE;timeout = 90"));

                       opt = opt->next;
                       REQUIRE_THAT(opt->option, Equals("Transport"));
                       REQUIRE_THAT(opt->content, Equals(fmt::format(" {}", state->audio_port)));
                     });
  }

  SECTION("SETUP video") {
    wolf_client->run("SETUP streamid=video/0/0 RTSP/1.0\n"
                     "CSeq: 4\n"
                     "X-GS-ClientVersion: 14\n"
                     "Host: 10.1.2.49\n"
                     "Session:  DEADBEEFCAFE\n"
                     "Transport: unicast;X-GS-ClientPort=50000-50001\n"
                     "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT"
                     "\r\n\r\n"sv,
                     [&state](std::optional<msg_t> response) {
                       REQUIRE(response);
                       REQUIRE(response.value()->message.response.statusCode == 200);
                       REQUIRE(response.value()->sequenceNumber == 4);

                       auto opt = response.value()->options->next;
                       REQUIRE_THAT(opt->option, Equals("Session"));
                       REQUIRE_THAT(opt->content, Equals(" DEADBEEFCAFE;timeout = 90"));

                       opt = opt->next;
                       REQUIRE_THAT(opt->option, Equals("Transport"));
                       REQUIRE_THAT(opt->content, Equals(fmt::format(" {}", state->video_port)));
                     });
  }

  SECTION("SETUP control") {
    wolf_client->run("SETUP streamid=control/0/0 RTSP/1.0\n"
                     "CSeq: 5\n"
                     "X-GS-ClientVersion: 14\n"
                     "Host: 10.1.2.49\n"
                     "Session:  DEADBEEFCAFE\n"
                     "Transport: unicast;X-GS-ClientPort=50000-50001\n"
                     "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT"
                     "\r\n\r\n"sv,
                     [&state](std::optional<msg_t> response) {
                       REQUIRE(response);
                       REQUIRE(response.value()->message.response.statusCode == 200);
                       REQUIRE(response.value()->sequenceNumber == 5);

                       auto opt = response.value()->options->next;
                       REQUIRE_THAT(opt->option, Equals("Session"));
                       REQUIRE_THAT(opt->content, Equals(" DEADBEEFCAFE;timeout = 90"));

                       opt = opt->next;
                       REQUIRE_THAT(opt->option, Equals("Transport"));
                       REQUIRE_THAT(opt->content, Equals(fmt::format(" {}", state->control_port)));
                     });
  }

  SECTION("ANNOUNCE control") {
    // This is a very long message, it'll kick the recursion in receive_message()
    wolf_client->run("ANNOUNCE streamid=control/13/0 RTSP/1.0\n"
                     "CSeq: 6\n"
                     "X-GS-ClientVersion: 14\n"
                     "Host: 0.0.0.0\n"
                     "Session:  DEADBEEFCAFE\n"
                     "Content-type: application/sdp\n"
                     "Content-length: 1308"
                     "\r\n\r\n" // start of payload
                     "v=0\n"
                     "o=android 0 14 IN IPv4 0.0.0.0\n"
                     "s=NVIDIA Streaming Client\n"
                     "a=x-nv-video[0].clientViewportWd:1920 \n"
                     "a=x-nv-video[0].clientViewportHt:1080 \n"
                     "a=x-nv-video[0].maxFPS:60 \n"
                     "a=x-nv-video[0].packetSize:1024 \n"
                     "a=x-nv-video[0].rateControlMode:4 \n"
                     "a=x-nv-video[0].timeoutLengthMs:7000 \n"
                     "a=x-nv-video[0].framesWithInvalidRefThreshold:0 \n"
                     "a=x-nv-video[0].initialBitrateKbps:15500 \n"
                     "a=x-nv-video[0].initialPeakBitrateKbps:15500 \n"
                     "a=x-nv-vqos[0].bw.minimumBitrateKbps:15500 \n"
                     "a=x-nv-vqos[0].bw.maximumBitrateKbps:15500 \n"
                     "a=x-nv-vqos[0].fec.enable:1 \n"
                     "a=x-nv-vqos[0].videoQualityScoreUpdateTime:5000 \n"
                     "a=x-nv-vqos[0].qosTrafficType:0 \n"
                     "a=x-nv-aqos.qosTrafficType:0 \n"
                     "a=x-nv-general.featureFlags:167 \n"
                     "a=x-nv-general.useReliableUdp:13 \n"
                     "a=x-nv-vqos[0].fec.minRequiredFecPackets:2 \n"
                     "a=x-nv-vqos[0].drc.enable:0 \n"
                     "a=x-nv-general.enableRecoveryMode:0 \n"
                     "a=x-nv-video[0].videoEncoderSlicesPerFrame:1 \n"
                     "a=x-nv-clientSupportHevc:0 \n"
                     "a=x-nv-vqos[0].bitStreamFormat:0 \n"
                     "a=x-nv-video[0].dynamicRangeMode:0 \n"
                     "a=x-nv-video[0].maxNumReferenceFrames:1 \n"
                     "a=x-nv-video[0].clientRefreshRateX100:0 \n"
                     "a=x-nv-audio.surround.numChannels:2 \n"
                     "a=x-nv-audio.surround.channelMask:3 \n"
                     "a=x-nv-audio.surround.enable:0 \n"
                     "a=x-nv-audio.surround.AudioQuality:0 \n"
                     "a=x-nv-aqos.packetDuration:5 \n"
                     "a=x-nv-video[0].encoderCscMode:0 \n"
                     "t=0 0\n"
                     "m=video 47998 \n"sv,
                     [](std::optional<msg_t> response) {
                       REQUIRE(response);
                       REQUIRE(response.value()->message.response.statusCode == 200);
                       REQUIRE(response.value()->sequenceNumber == 6);
                     });
  }
}