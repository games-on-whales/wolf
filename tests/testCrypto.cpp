#include "catch2/catch_all.hpp"
using Catch::Matchers::Equals;

#include <algorithm>
#include <crypto/crypto.hpp>
#include <moonlight/protocol.hpp>
#include <string>

using namespace std::string_literals;

TEST_CASE("sha256", "[Crypto]") {
  REQUIRE(crypto::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  REQUIRE(crypto::sha256("The quick brown fox jumps over the lazy dog") ==
          "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST_CASE("str and hex", "[Crypto]") {
  SECTION("str to hex") {
    REQUIRE(crypto::str_to_hex("").empty());
    REQUIRE(crypto::str_to_hex("-----BEGIN CERTIFICATE-----") ==
            "2D2D2D2D2D424547494E2043455254494649434154452D2D2D2D2D");
  }

  SECTION("hex to str") {
    std::string client_cert =
        "2d2d2d2d2d424547494e2043455254494649434154452d2d2d2d2d0a4d494943767a43434161656741774942416749424144414e42676b"
        "71686b694739773042415173464144416a4d53457748775944565151444442684f566b6c450a53554567523246745a564e30636d566862"
        "53424462476c6c626e51774868634e4d6a45774e7a45774d44677a4e6a45335768634e4e4445774e7a41314d44677a0a4e6a4533576a41"
        "6a4d53457748775944565151444442684f566b6c4553554567523246745a564e30636d56686253424462476c6c626e5177676745694d41"
        "30470a4353714753496233445145424151554141344942447741776767454b416f4942415143316334396f4a78506b49476a554e537745"
        "704e75764e5a422f68526f650a445a504d6533675a3161624f534a46794b335662514e534f564b367145664272487151674e564d74585a"
        "565335584466445257394533375163416c35417065780a2f4e414732664e485941764f365036476a2b2f6d30584257635652457a6a4743"
        "644b4a492b39546f4e6542366451744b7732364c766c496248667072425a2b310a5150723344755575544349647861634a72536f557971"
        "616b7377477238326a332b4474336455304c574371755665654f4d684d564e2b45467a6a4662444a627a0a2b734e4c544d32453748654b"
        "4e6549722f306f466879413262354b623476642b7a3147665733386f3835674455386e6936464f6d45614654693354645a3179520a5278"
        "644b4b64396b2b6d79434e484c69794241394b3451324a5975556b59785a7a735a724b5648374c576648373971614d766a383769456e41"
        "674d42414145770a4451594a4b6f5a496876634e4151454c42514144676745424144566d3030356a71694645477548364a66544b52635a"
        "5533504d6d4672456d4a546d76506979680a61346237356a616930684c52437a4e44753662537a6c5848333436594b2f783741566b535a"
        "496f4432685470756a566c4849484c6f534a7a6b726d4d784946520a735a526a6830454761416d764475455841445545304d47706f574c"
        "5551756d69445777755362457a507972374249443638413351366a4b7764363544322b75740a736e4c7a6c4572546c356667486b4c5756"
        "36706c656c634d6750536f34542b453741506d344c486c5035757869697841666e68446c7a754165443572317273440a556b724e664d66"
        "586e527a50537758694e59485a2b556f517563686f4d435341612b6b6373512b7a73645077414a337374775147636670765968645a7631"
        "61310a6f504a70794369676b6d763075483443654a3039412f3644613275592b484977567138357174654d565355547456303d0a2d2d2d"
        "2d2d454e442043455254494649434154452d2d2d2d2d0a";

    REQUIRE(crypto::hex_to_str(client_cert, true) ==
            "-----BEGIN CERTIFICATE-----\n"
            "MIICvzCCAaegAwIBAgIBADANBgkqhkiG9w0BAQsFADAjMSEwHwYDVQQDDBhOVklE\n"
            "SUEgR2FtZVN0cmVhbSBDbGllbnQwHhcNMjEwNzEwMDgzNjE3WhcNNDEwNzA1MDgz\n"
            "NjE3WjAjMSEwHwYDVQQDDBhOVklESUEgR2FtZVN0cmVhbSBDbGllbnQwggEiMA0G\n"
            "CSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC1c49oJxPkIGjUNSwEpNuvNZB/hRoe\n"
            "DZPMe3gZ1abOSJFyK3VbQNSOVK6qEfBrHqQgNVMtXZVS5XDfDRW9E37QcAl5Apex\n"
            "/NAG2fNHYAvO6P6Gj+/m0XBWcVREzjGCdKJI+9ToNeB6dQtKw26LvlIbHfprBZ+1\n"
            "QPr3DuUuTCIdxacJrSoUyqakswGr82j3+Dt3dU0LWCquVeeOMhMVN+EFzjFbDJbz\n"
            "+sNLTM2E7HeKNeIr/0oFhyA2b5Kb4vd+z1GfW38o85gDU8ni6FOmEaFTi3TdZ1yR\n"
            "RxdKKd9k+myCNHLiyBA9K4Q2JYuUkYxZzsZrKVH7LWfH79qaMvj87iEnAgMBAAEw\n"
            "DQYJKoZIhvcNAQELBQADggEBADVm005jqiFEGuH6JfTKRcZU3PMmFrEmJTmvPiyh\n"
            "a4b75jai0hLRCzNDu6bSzlXH346YK/x7AVkSZIoD2hTpujVlHIHLoSJzkrmMxIFR\n"
            "sZRjh0EGaAmvDuEXADUE0MGpoWLUQumiDWwuSbEzPyr7BID68A3Q6jKwd65D2+ut\n"
            "snLzlErTl5fgHkLWV6plelcMgPSo4T+E7APm4LHlP5uxiixAfnhDlzuAeD5r1rsD\n"
            "UkrNfMfXnRzPSwXiNYHZ+UoQuchoMCSAa+kcsQ+zsdPwAJ3stwQGcfpvYhdZv1a1\n"
            "oPJpyCigkmv0uH4CeJ09A/6Da2uY+HIwVq85qteMVSUTtV0=\n"
            "-----END CERTIFICATE-----\n");
  }

  SECTION("back and forth") {
    auto original_str = "a very complex string";
    REQUIRE(crypto::hex_to_str(crypto::str_to_hex(original_str), true) == original_str);
  }
}

TEST_CASE("AES ecb", "[Crypto]") {
  auto key = "0123456789012345"s;
  auto msg = "a message to be sent!"s;
  auto iv = "12345678"s;

  auto encrypted = crypto::aes_encrypt_ecb(msg, key, iv, true);

  REQUIRE(crypto::str_to_hex(encrypted) == "ABAF3D0AEE0FEDE3955EA4BBE190B5817777A7F53C3A0BF3258967E547285A9A");
  REQUIRE(crypto::aes_decrypt_ecb(encrypted, key, iv, true) == msg);

  SECTION("Moonlight simulation") {
    auto salt = "ff5dc6eda99339a8a0793e216c4257c4"s;
    auto pin = "5338";
    auto client_challenge = "c05930ac81d7bd426344235436046018";

    auto aes_key = moonlight::pair::gen_aes_key(salt, pin);
    REQUIRE(crypto::str_to_hex(aes_key) == "5EA186FFBA663C75AEC82187CE502647");

    auto client_challenge_hex = crypto::hex_to_str(client_challenge, true);
    auto decrypted_challenge = crypto::aes_decrypt_ecb(client_challenge_hex, aes_key, "12345678", false);
    REQUIRE(crypto::str_to_hex(decrypted_challenge) == "E3A915CCCB4C60206077D7E9A12316A5");
  }
}

TEST_CASE("AES gcm", "[Crypto]") {
  auto key = "0123456789012345"s;
  auto msg = "a message to be sent!"s;
  auto iv = "12345678"s;

  auto [encrypted, tag] = crypto::aes_encrypt_gcm(msg, key, iv, true);
  REQUIRE(crypto::aes_decrypt_gcm(encrypted, key, tag, iv, -1, true) == msg);
}

TEST_CASE("AES cbc", "[Crypto]") {
  auto key = "0123456789012345"s;
  auto msg = "a message to be sent!"s;
  auto iv = "12345678"s;

  auto encrypted = crypto::aes_encrypt_cbc(msg, key, iv, true);
  REQUIRE(crypto::aes_decrypt_cbc(encrypted, key, iv, true) == msg);
}

TEST_CASE("OpenSSL sign", "[Crypto]") {
  SECTION("Manually created") { /* Generated using: `openssl genrsa -out mykey.pem 1024` */
    auto private_key = "-----BEGIN RSA PRIVATE KEY-----\n"
                       "MIICXAIBAAKBgQDwRIo9jwMkSxUPLbuLSnUpy4yoRkA8L1NGitnQjrol9ouz436k\n"
                       "7Ip0jyEqAqLdmSAJYtoyqx78BbPP1ubscrKwjWVxf67FeZBWavhkhNhZoaXWYmhN\n"
                       "Pif5OlvI6WFasg4L6IDGOh/Gl6SrqUntYsYLqJmvfuJf175zdS3YCribdwIDAQAB\n"
                       "AoGBANVI8rK8vlw8boBf54k52pH0iHNkkWcb17/aSIrz+Fj06IUS4PyEok/gMt95\n"
                       "IZy3bpIGd43dDA9K/Jj2u12QX//Tx96DbJznmjMeOTEJY/+Hl7rfNGEchUyBsZeP\n"
                       "vW5KdIHKGaoLkXnuDtsQjq63nzkPEa9Ijl7dFYtUZ9FdQvsBAkEA/YDRefFlbkXJ\n"
                       "CMWzD/x+s/YEbvBwW5qA9V5qaCpdRjHhdOuZ6VDUl627IktF3L4QfUPlCWJKUSeW\n"
                       "670X9hxggQJBAPKiWZNgzIH7VpD+mbxneGW1wjGRx5MO6AntdxINhnC8jFJcw6Jl\n"
                       "y+GthxoA07OuSjdU5oKfrUZTGBLzf9W3//cCQBS0ted48Sj9qDsAMu0GWa8HVDtf\n"
                       "hj3lM81W5egWNcIrBthO+iZVhNfSx+s4LL+oAp7Ised/UMSqMCiXLGLc1IECQEH0\n"
                       "nfLxEkaXIv4BJ5tOaS0EzogY/65bE/p24bI3mP8WUfKlosyHbXeoaxxHc0TZsPT/\n"
                       "kDWb4EdImTe1l19qSBsCQBBq6j2aIC2MMKatQ4916tGxDdfJrUKpujtqypOeVCu4\n"
                       "TrWeVb6zYtY5BC6Y+AzitAkHLg+gZ8df6B5z4cgATOs=\n"
                       "-----END RSA PRIVATE KEY-----"s;
    /* Generated from the private key using: `openssl rsa -in mykey.pem -pubout > mykey.pub` */
    auto public_key = "-----BEGIN PUBLIC KEY-----\n"
                      "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDwRIo9jwMkSxUPLbuLSnUpy4yo\n"
                      "RkA8L1NGitnQjrol9ouz436k7Ip0jyEqAqLdmSAJYtoyqx78BbPP1ubscrKwjWVx\n"
                      "f67FeZBWavhkhNhZoaXWYmhNPif5OlvI6WFasg4L6IDGOh/Gl6SrqUntYsYLqJmv\n"
                      "fuJf175zdS3YCribdwIDAQAB\n"
                      "-----END PUBLIC KEY-----"s;

    auto msg = "A very important message to be signed"s;
    auto signature = crypto::sign(msg, private_key);

    REQUIRE(crypto::str_to_hex(signature) ==
            "BE6EDF421CEFC1D0AFFB88487A2A23FA0B12DAABAE87D263F0F9A8F36758D30FE52EE475FFE"
            "A11D00DED565406807968E8F14A8D3C1DC0E01E3D71B0AE7495232F425E1CA62F403069164A1"
            "1225F18CA5472932BE34A82A78BC0A06CE7503AAD2EC7BA5A77A0A8A1D3F83623EE1D3F89EB4"
            "F10B0B72642FB88CD08C055D64CFE");

    REQUIRE(crypto::verify(msg, signature, public_key));
  }
  SECTION("Auto generated") {
    auto pkey = x509::generate_key();
    auto pcert = x509::generate_x509(pkey);

    auto private_key = x509::get_pkey_content(pkey);
    auto public_cert = x509::get_cert_public_key(pcert);

    auto msg = "A very important message to be signed"s;
    auto signature = crypto::sign(msg, private_key);

    REQUIRE(crypto::verify(msg, signature, public_cert));
  }
}