#include "catch2/catch_all.hpp"
#include <fake-udev/fake-udev.hpp>

using Catch::Matchers::Equals;
using namespace std::string_literals;

TEST_CASE("Helper methods", "[fake-udev]") {
  REQUIRE(string_hash32("input") == 3248653424);

  REQUIRE_THAT(
      base64_decode("LklOUFVUX0NMQVNTPWpveXN0aWNrAEFDVElPTj1hZGQAQ1VSUkVOVF9UQUdTPTpzZWF0OnVhY2Nlc3M6AERFVk5BTUU9L2Rldi"
                    "9pbnB1dC9ldmVudDIzAERFVlBBVEg9L2RldmljZXMvdmlydHVhbC9pbnB1dC9pbnB1dDM4Ny9ldmVudDIzAElEX0lOUFVUPTEA"
                    "SURfSU5QVVRfSk9ZU1RJQ0s9MQBJRF9TRVJJQUw9bm9zZXJpYWwATUFKT1I9MTMATUlOT1I9ODcAU0VRTlVNPTcAU1VCU1lTVE"
                    "VNPWlucHV0AFRBR1M9OnNlYXQ6dWFjY2VzczoAVVNFQ19JTklUSUFMSVpFRD0xNjk1OTA4ODIxAA"),
      Equals(".INPUT_CLASS=joystick\0"
             "ACTION=add\0"
             "CURRENT_TAGS=:seat:uaccess:\0"
             "DEVNAME=/dev/input/event23\0"
             "DEVPATH=/devices/virtual/input/input387/event23\0"
             "ID_INPUT=1\0"
             "ID_INPUT_JOYSTICK=1\0"
             "ID_SERIAL=noserial\0"
             "MAJOR=13\0"
             "MINOR=87\0"
             "SEQNUM=7\0"
             "SUBSYSTEM=input\0"
             "TAGS=:seat:uaccess:\0"
             "USEC_INITIALIZED=1695908821\0"s));
}