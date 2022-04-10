#include <boost/property_tree/xml_parser.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <moonlight/crypto.hpp>
#include <moonlight/protocol.hpp>
#include <rest/x509.cpp>

using namespace moonlight;

TEST_CASE("LocalState load JSON", "[LocalState]") {
  auto state = new Config("config.json");
  REQUIRE(state->hostname() == "test_wolf");
  REQUIRE(state->get_uuid() == "uid-12345");
  REQUIRE(state->external_ip() == "192.168.99.1");
  REQUIRE(state->local_ip() == "192.168.1.1");
  REQUIRE(state->mac_address() == "AA:BB:CC:DD");

  SECTION("Port mapping") {
    REQUIRE(state->map_port(Config::HTTP_PORT) == 3000);
    REQUIRE(state->map_port(Config::HTTPS_PORT) == 2995);
  }
}

TEST_CASE("LocalState pairing information", "[LocalState]") {
  auto state = new Config("config.json");
  auto clientID = "0123456789ABCDEF";
  auto a_client_cert = "A DUMP OF A VALID CERTIFICATE";

  SECTION("Checking pairing mechanism") {
    REQUIRE(state->get_paired_clients().empty());
    REQUIRE(state->isPaired(clientID) == false);

    state->pair("Another client", a_client_cert);
    REQUIRE(state->isPaired(clientID) == false);
    state->pair(clientID, a_client_cert);
    REQUIRE(state->isPaired(clientID) == true);
    REQUIRE(state->isPaired("Another client") == true);

    REQUIRE_THAT(state->get_paired_clients(), Catch::Matchers::SizeIs(2)); // TODO: check content
  }

  SECTION("Checking client cert info") {
    REQUIRE(state->get_client_cert(clientID) == std::nullopt);

    state->pair(clientID, a_client_cert);
    state->pair("Another client", a_client_cert);

    REQUIRE(state->get_client_cert(clientID) == a_client_cert);
    REQUIRE(state->get_client_cert(clientID) == a_client_cert);
    REQUIRE(state->get_client_cert("Another client") == a_client_cert);
    REQUIRE(state->get_client_cert("A non existent client") == std::nullopt);
  }
}

TEST_CASE("Mocked serverinfo", "[MoonlightProtocol]") {
  auto state = new Config("config.json");
  std::vector<DisplayMode> displayModes = {{1920, 1080, 60}, {1024, 768, 30}};

  SECTION("server_info conforms with the expected server_info_response.xml") {
    auto result = serverinfo(*state, false, 0, displayModes, "001122");
    pt::ptree expectedResult;
    pt::read_xml("server_info_response.xml", expectedResult, boost::property_tree::xml_parser::trim_whitespace);

    REQUIRE(result == expectedResult);
    REQUIRE(result.get<bool>("root.PairStatus") == false);
  }

  SECTION("does pairing change the returned serverinfo?") {
    state->pair("001122", "");
    auto result = serverinfo(*state, false, 0, displayModes, "001122");

    REQUIRE(result.get<bool>("root.PairStatus") == true);
  }
}

TEST_CASE("Pairing protocol", "[MoonlightProtocol]") {
  // Stuff generated from Moonlight
  auto pin = "7284";
  auto salt = "a0c288cfb0ea624ec3e5cc54d6ab7e38";
  auto client_challenge = "60418ac415307d7a1f9695ba04e1ae34";
  auto server_challenge_resp = "2798e4a56fa102558e8c93b892fbbe17b06b0cac91855ceb536ca2def53f7cf1";
  auto client_pairing_secret =
      "3dda32b87413e98005d608df9c54323f507ccaef60a5a31acfc2b9bc64bb27e3cc467555bf81bc0464dc4b1488810d435266f31e22acd275"
      "07bce4a8173d1b4c5ea51f47d702a074e9cf95c6ff6cb7cf85e1abf9d236c5e3b638601092adc6a993f77b0585100994cecec88e01023f7f"
      "0ef13e8d66f5ba89608f1838c85d214b91a4be276d9ad555382a599b142b1e62b571427dc30697342da9995bfc5362cc27f09a3cb3622eda"
      "9f6ccad3385e296e5b9b296377523e0e41b5748d60ea96f2ebabb9c30c13e98cef420d87e266f72bfc2e18cf584a03e6141e9dd4967dd77e"
      "4ce99b20f73e37caf801931babbc4ff75df4013b529651b97d7bfc228d2c13785c50e22071bc6ce1332ee777a009c630";
  auto client_cert_base64 =
      "2d2d2d2d2d424547494e2043455254494649434154452d2d2d2d2d0a4d494943767a43434161656741774942416749424144414e42676b71"
      "686b694739773042415173464144416a4d53457748775944565151444442684f566b6c450a53554567523246745a564e30636d5668625342"
      "4462476c6c626e51774868634e4d6a45774e7a45774d44677a4e6a45335768634e4e4445774e7a41314d44677a0a4e6a4533576a416a4d53"
      "457748775944565151444442684f566b6c4553554567523246745a564e30636d56686253424462476c6c626e5177676745694d4130470a43"
      "53714753496233445145424151554141344942447741776767454b416f4942415143316334396f4a78506b49476a554e537745704e75764e"
      "5a422f68526f650a445a504d6533675a3161624f534a46794b335662514e534f564b367145664272487151674e564d74585a565335584466"
      "445257394533375163416c35417065780a2f4e414732664e485941764f365036476a2b2f6d30584257635652457a6a4743644b4a492b3954"
      "6f4e6542366451744b7732364c766c496248667072425a2b310a5150723344755575544349647861634a72536f557971616b737747723832"
      "6a332b4474336455304c574371755665654f4d684d564e2b45467a6a4662444a627a0a2b734e4c544d32453748654b4e6549722f306f4668"
      "79413262354b623476642b7a3147665733386f3835674455386e6936464f6d45614654693354645a3179520a5278644b4b64396b2b6d7943"
      "4e484c69794241394b3451324a5975556b59785a7a735a724b5648374c576648373971614d766a383769456e41674d42414145770a445159"
      "4a4b6f5a496876634e4151454c42514144676745424144566d3030356a71694645477548364a66544b52635a5533504d6d4672456d4a546d"
      "76506979680a61346237356a616930684c52437a4e44753662537a6c5848333436594b2f783741566b535a496f4432685470756a566c4849"
      "484c6f534a7a6b726d4d784946520a735a526a6830454761416d764475455841445545304d47706f574c5551756d69445777755362457a50"
      "7972374249443638413351366a4b7764363544322b75740a736e4c7a6c4572546c356667486b4c575636706c656c634d6750536f34542b45"
      "3741506d344c486c5035757869697841666e68446c7a754165443572317273440a556b724e664d66586e527a50537758694e59485a2b556f"
      "517563686f4d435341612b6b6373512b7a73645077414a337374775147636670765968645a763161310a6f504a70794369676b6d76307548"
      "3443654a3039412f3644613275592b484977567138357174654d565355547456303d0a2d2d2d2d2d454e442043455254494649434154452d"
      "2d2d2d2d0a";

  // Stuff generated on our side
  auto server_secret = crypto::hex_to_str("AB7D178785415C893A6757C1B736105A", true);
  auto server_cert = x509::cert_from_string("-----BEGIN CERTIFICATE-----\n"
                                            "MIIC6zCCAdOgAwIBAgIBATANBgkqhkiG9w0BAQsFADA5MQswCQYDVQQGEwJJVDEW\n"
                                            "MBQGA1UECgwNR2FtZXNPbldoYWxlczESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTIy\n"
                                            "MDQwOTA5MTYwNVoXDTQyMDQwNDA5MTYwNVowOTELMAkGA1UEBhMCSVQxFjAUBgNV\n"
                                            "BAoMDUdhbWVzT25XaGFsZXMxEjAQBgNVBAMMCWxvY2FsaG9zdDCCASIwDQYJKoZI\n"
                                            "hvcNAQEBBQADggEPADCCAQoCggEBAMt482VY3ToUuUy6NbMhfxQgI7tJZ8fkNeVp\n"
                                            "9WOnHCL9YKR07oXGLGpE0a7vXAy8lcVsOU1Hx+pfbGj56rXsne4Uqf6p2OY/cvfx\n"
                                            "uSrGGgn+cKteR4bIJND4Nq6DrdlhIl5bYyZ/4sBHn+L99Zh9elKVtx/lclA8Ra8Q\n"
                                            "2kupa7405TnR0lcgRVilRdHHb7HhlvCQfu1Umb3gv4I5TKIkpA/JaBTZoWzIkbAc\n"
                                            "V9499JSl9gepsdlX8guljn1UlqKsHAT31vH+YG8wjtqEGYlNIO4N98lw8OEUXmRl\n"
                                            "rRSRA+s++FdxBpJG2Lu/RWicRCPylNKcZiv2S1YqT3bDEPKf1LcCAwEAATANBgkq\n"
                                            "hkiG9w0BAQsFAAOCAQEAqPBqzvDjl89pZMll3Ge8RS7HeDuzgocrhOcT2jnk4ag7\n"
                                            "/TROZuISjDp6+SnL3gPEt7E2OcFAczTg3l/wbT5PFb6vM96saLm4EP0zmLfK1FnM\n"
                                            "JDRahKutP9rx6RO5OHqsUB+b4jA4W0L9UnXUoLKbjig501AUix0p52FBxu+HJ90r\n"
                                            "HlLs3Vo6nj4Z/PZXrzaz8dtQ/KJMpd/g/9xlo6BKAnRk5SI8KLhO4hW6zG0QA56j\n"
                                            "X4wnh1bwdiidqpcgyuKossLOPxbS786WmsesaAWPnpoY6M8aija+ALwNNuWWmyMg\n"
                                            "9SVDV76xJzM36Uq7Kg3QJYTlY04WmPIdJHkCtXWf9g==\n"
                                            "-----END CERTIFICATE-----");
  auto server_pkey = "-----BEGIN PRIVATE KEY-----\n"
                     "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDLePNlWN06FLlM\n"
                     "ujWzIX8UICO7SWfH5DXlafVjpxwi/WCkdO6FxixqRNGu71wMvJXFbDlNR8fqX2xo\n"
                     "+eq17J3uFKn+qdjmP3L38bkqxhoJ/nCrXkeGyCTQ+Daug63ZYSJeW2Mmf+LAR5/i\n"
                     "/fWYfXpSlbcf5XJQPEWvENpLqWu+NOU50dJXIEVYpUXRx2+x4ZbwkH7tVJm94L+C\n"
                     "OUyiJKQPyWgU2aFsyJGwHFfePfSUpfYHqbHZV/ILpY59VJairBwE99bx/mBvMI7a\n"
                     "hBmJTSDuDffJcPDhFF5kZa0UkQPrPvhXcQaSRti7v0VonEQj8pTSnGYr9ktWKk92\n"
                     "wxDyn9S3AgMBAAECggEAbEhQ14WELg2rUz7hpxPTaiV0fo4hEcrMN+u8sKzVF3Xa\n"
                     "QYsNCNoe9urq3/r39LtDxU3D7PGfXYYszmz50Jk8ruAGW8WN7XKkv3i/fxjv8JOc\n"
                     "6EYDMKJAnYkKqLLhCQddX/Oof2udg5BacVWPpvhX6a1NSEc2H6cDupfwZEWkVhMi\n"
                     "bCC3JcNmjFa8N7ow1/5VQiYVTjpxfV7GY1GRe7vMvBucdQKH3tUG5PYXKXytXw/j\n"
                     "KDLaECiYVT89KbApkI0zhy7I5g3LRq0Rs5fmYLCjVebbuAL1W5CJHFJeFOgMKvnO\n"
                     "QSl7MfHkTnzTzUqwkwXjgNMGsTosV4UloL9gXVF6GQKBgQD5fI771WETkpaKjWBe\n"
                     "6XUVSS98IOAPbTGpb8CIhSjzCuztNAJ+0ey1zklQHonMFbdmcWTkTJoF3ECqAos9\n"
                     "vxB4ROg+TdqGDcRrXa7Twtmhv66QvYxttkaK3CqoLX8CCTnjgXBCijo6sCpo6H1T\n"
                     "+y55bBDpxZjNFT5BV3+YPBfWQwKBgQDQyNt+saTqJqxGYV7zWQtOqKORRHAjaJpy\n"
                     "m5035pky5wORsaxQY8HxbsTIQp9jBSw3SQHLHN/NAXDl2k7VAw/axMc+lj9eW+3z\n"
                     "2Hv5LVgj37jnJYEpYwehvtR0B4jZnXLyLwShoBdRPkGlC5fs9+oWjQZoDwMLZfTg\n"
                     "eZVOJm6SfQKBgQDfxYcB/kuKIKsCLvhHaSJpKzF6JoqRi6FFlkScrsMh66TCxSmP\n"
                     "0n58O0Cqqhlyge/z5LVXyBVGOF2Pn6SAh4UgOr4MVAwyvNp2aprKuTQ2zhSnIjx4\n"
                     "k0sGdZ+VJOmMS/YuRwUHya+cwDHp0s3Gq77tja5F38PD/s/OD8sUIqJGvQKBgBfI\n"
                     "6ghy4GC0ayfRa+m5GSqq14dzDntaLU4lIDIAGS/NVYDBhunZk3yXq99Mh6/WJQVf\n"
                     "Uc77yRsnsN7ekeB+as33YONmZm2vd1oyLV1jpwjfMcdTZHV8jKAGh1l4ikSQRUoF\n"
                     "xTdMb5uXxg6xVWtvisFq63HrU+N2iAESmMnAYxRZAoGAVEFJRRjPrSIUTCCKRiTE\n"
                     "br+cHqy6S5iYRxGl9riKySBKeU16fqUACIvUqmqlx4Secj3/Hn/VzYEzkxcSPwGi\n"
                     "qMgdS0R+tacca7NopUYaaluneKYdS++DNlT/m+KVHqLynQr54z1qBlThg9KGrpmM\n"
                     "LGZkXtQpx6sX7v3Kq56PkNk=\n"
                     "-----END PRIVATE KEY-----";

  // PHASE 1
  auto [xml_p1, aes_key] = moonlight::pair_get_server_cert(pin, salt, *server_cert);
  REQUIRE(crypto::str_to_hex(aes_key) == "8A0191F59F31950D5DE3396901AA585D");

  // PHASE 2
  auto server_cert_sig = x509::get_cert_signature(server_cert);
  auto [xml_p2, server_secret_pair] =
      moonlight::pair_send_server_challenge(aes_key, client_challenge, server_cert_sig, server_secret, server_secret);
  auto [server_secret_ret, server_challenge_ret] = server_secret_pair;
  REQUIRE(server_secret == server_secret_ret);
  REQUIRE(server_secret == server_challenge_ret);
  REQUIRE(xml_p2.get<std::string>("root.challengeresponse") ==
          "2A86F54B35997EAE9CC30D3F06E66D93178FB7892180FEE2B4BBB8C68473BDE13401EFE6604815200DE9BEA7D2215FC4");

  // PHASE 3
  auto [xml_p3, client_hash] =
      moonlight::pair_get_client_hash(aes_key, server_secret, server_challenge_resp, server_pkey);
  REQUIRE(crypto::str_to_hex(client_hash) == "3875FFF759355205355EFE5D3065CD776A06A806CAAE0CEFAEF22475D593CEA4");
  auto pairing_secret = xml_p3.get<std::string>("root.pairingsecret");
  REQUIRE(
      pairing_secret ==
      "AB7D178785415C893A6757C1B736105A9D9B953D2D5DE21B5CA8483FFF677223A2C5E625510E16C6AA28B05E58B3EADC7D77EA32EE3684C4"
      "1C735D4F42B2FE2B5A4498988CAF01E1DFC42E801F19D8F8417CA4EFA3144E9972B948B451309F045E1C678330065D46E6C8260AD720663A"
      "FFC8DF985D8E5AB4D74CCEA266ED6A227C9F61BACF0968925A327368367EB5CF9507D21B85A3AB9F782A6DB927A013C6FD1C6201BFCE480D"
      "29BDCE706389AB4A735B5115F1E42A19FFB735617DD7EF2B46558DACDF352F33564C663E84D130F3FCBB81BC5CC125E3F772472F9A0350C1"
      "0D6B9B5ECEFE177514E8AF496E1E210FBC03571DDB222B3AA97E76BE5A7D7731756CFE3B1202237D38433F789F87B3FA");

  // PHASE 4
  auto client_cert = x509::cert_from_string(crypto::hex_to_str(client_cert_base64, true));
  auto client_public_cert_signature = x509::get_cert_signature(client_cert);
  auto client_cert_public_key = x509::get_cert_public_key(client_cert);
  auto xml_p4 = moonlight::pair_client_pair(aes_key,
                                            server_challenge_ret,
                                            client_hash,
                                            client_pairing_secret,
                                            client_public_cert_signature,
                                            client_cert_public_key);
  REQUIRE(xml_p4.get<int>("root.paired") == 1);
}
