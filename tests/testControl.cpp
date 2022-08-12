#include <control/packet_utils.hpp>
#include <crypto.hpp>

TEST_CASE("Control AES Encryption", "CONTROL") {
  // A bunch of packets taken from a real session

  // 30 bytes
  auto encrypted_packet = crypto::hex_to_str("01001A0000000000BF0EB6DA10E47C702EC8644EB87D9CF7B6FAC9FF75CA");
  auto aes_key = crypto::hex_to_str("EDF04A215C4FBEA20934120C8480D855");
  auto decrypted =
      control::decrypt_packet(reinterpret_cast<const enet_uint8 *>(encrypted_packet.data()), aes_key.data());
  REQUIRE_THAT(crypto::str_to_hex(decrypted), Equals("020302000000"));

  // 29 bytes
  encrypted_packet = crypto::hex_to_str("010019000100000021DBB8DC0590AF3A2B20BCE5A347DE31D366E5B9C5");
  aes_key = crypto::hex_to_str("EDF04A215C4FBEA20934120C8480D855");
  decrypted = control::decrypt_packet(reinterpret_cast<const enet_uint8 *>(encrypted_packet.data()), aes_key.data());
  REQUIRE_THAT(crypto::str_to_hex(decrypted), Equals("0703010000"));

  // 36 bytes
  encrypted_packet = crypto::hex_to_str("0100200002000000220722FBADED58A03F2E8898F0F1DCB7C93F6235590618E4186AD990");
  aes_key = crypto::hex_to_str("EDF04A215C4FBEA20934120C8480D855");
  decrypted = control::decrypt_packet(reinterpret_cast<const enet_uint8 *>(encrypted_packet.data()), aes_key.data());
  REQUIRE_THAT(crypto::str_to_hex(decrypted), Equals("000208000400000000000000"));

  // 46 bytes
  encrypted_packet = crypto::hex_to_str(
      "01002A00060000005A4D999FB2542F85BDD39D99F77EB825254569D2C04E21241B5CEC01BD3F93129718ECC1F153");
  aes_key = crypto::hex_to_str("EDF04A215C4FBEA20934120C8480D855");
  decrypted = control::decrypt_packet(reinterpret_cast<const enet_uint8 *>(encrypted_packet.data()), aes_key.data());
  REQUIRE_THAT(crypto::str_to_hex(decrypted), Equals("060212000000000E05000000033400C00000059F0329"));
}