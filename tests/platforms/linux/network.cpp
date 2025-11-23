#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <boost/asio/ip/address.hpp>
#include <platforms/hw.hpp>
#include <regex>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <fstream>
#include <unistd.h>

using Catch::Matchers::Equals;

bool is_valid_mac_format(const std::string& mac) {
    std::regex mac_pattern("^([0-9a-f]{2}:){5}[0-9a-f]{2}$");
    return std::regex_match(mac, mac_pattern);
}

bool is_default_mac(const std::string& mac) {
    return mac == "00:00:00:00:00:00";
}

class DummyInterface {
private:
    std::string interface_name;
    std::string mac_address;
    bool created = false;
    
    int run_command(const std::string& cmd) {
        return std::system((cmd + " 2>/dev/null").c_str());
    }
    
    bool create_interface() {
        if (created) {
            return true;
        }
        
        run_command("modprobe dummy");
        
        if (run_command("ip link add " + interface_name + " type dummy") != 0) {
            return false;
        }
        
        if (run_command("ip link set " + interface_name + " up") != 0) {
            run_command("ip link delete " + interface_name);
            return false;
        }
        
        created = true;
        return true;
    }
    
    void fetch_mac_address() {
        if (created) {
            mac_address = get_mac_address_for_interface();
        }
    }
    
    std::string get_mac_address_for_interface() {
        // Read MAC address directly from sysfs
        std::string mac_path = "/sys/class/net/" + interface_name + "/address";
        std::ifstream mac_file(mac_path);
        if (mac_file.is_open()) {
            std::string mac;
            std::getline(mac_file, mac);
            mac_file.close();
            return mac;
        }
        return "00:00:00:00:00:00";
    }

public:
    explicit DummyInterface(const std::string& name) : interface_name(name) {
        if (!create_interface()) {
            throw std::runtime_error("Failed to create dummy interface: " + name);
        }
        fetch_mac_address();
    }
    
    DummyInterface(const std::string& name, const std::string& ipv4_address) : interface_name(name) {
        if (!create_interface()) {
            throw std::runtime_error("Failed to create dummy interface: " + name);
        }
        if (!add_ipv4_address(ipv4_address)) {
            throw std::runtime_error("Failed to add IPv4 address " + ipv4_address + " to interface " + name);
        }
        fetch_mac_address();
    }
    
    ~DummyInterface() {
        if (created) {
            run_command("ip link delete " + interface_name);
        }
    }
    
    DummyInterface(const DummyInterface&) = delete;
    DummyInterface& operator=(const DummyInterface&) = delete;
    
    DummyInterface(DummyInterface&& other) noexcept 
        : interface_name(std::move(other.interface_name)), 
          mac_address(std::move(other.mac_address)),
          created(other.created) {
        other.created = false;
    }
    
    DummyInterface& operator=(DummyInterface&& other) noexcept {
        if (this != &other) {
            if (created) {
                run_command("ip link delete " + interface_name);
            }
            
            interface_name = std::move(other.interface_name);
            mac_address = std::move(other.mac_address);
            created = other.created;
            other.created = false;
        }
        return *this;
    }
    
    bool add_ipv4_address(const std::string& ipv4_address) {
        if (!created) {
            return false;
        }
        
        return run_command("ip addr add " + ipv4_address + "/24 dev " + interface_name) == 0;
    }
    
    bool add_ipv6_address(const std::string& ipv6_address) {
        if (!created) {
            return false;
        }
        
        return run_command("ip addr add " + ipv6_address + "/64 dev " + interface_name) == 0;
    }
    
    const std::string& name() const { return interface_name; }
    const std::string& mac() const { return mac_address; }
    bool is_created() const { return created; }
};

TEST_CASE("get_mac_address: Valid IP addresses", "[hw_linux]") {
    if (geteuid() != 0) {
        SKIP("Root privileges required for dummy interface tests");
    }
    
    SECTION("Valid IPv4 addresses should return non-default MAC") {
        DummyInterface dummy("test_ipv4", "192.168.100.1");
        
        REQUIRE(is_valid_mac_format(dummy.mac()));
        REQUIRE_FALSE(is_default_mac(dummy.mac()));
        
        auto result = get_mac_address("192.168.100.1");

        REQUIRE_THAT(result, Equals(dummy.mac()));
    }
    
    SECTION("Valid IPv6 addresses should return non-default MAC") {
        DummyInterface dummy("test_ipv6");
        REQUIRE(dummy.add_ipv6_address("2001:db8::1"));
        
        REQUIRE(is_valid_mac_format(dummy.mac()));
        REQUIRE_FALSE(is_default_mac(dummy.mac()));
        
        auto result = get_mac_address("2001:db8::1");

        REQUIRE_THAT(result, Equals(dummy.mac()));
    }
    
    SECTION("IPv4-mapped IPv6 consistency") {
        DummyInterface dummy("test_mapped", "192.168.100.10");
        
        auto ipv4_result = get_mac_address("192.168.100.10");
        auto ipv6_mapped_result = get_mac_address("::ffff:192.168.100.10");
        
        REQUIRE(is_valid_mac_format(ipv4_result));
        REQUIRE(is_valid_mac_format(ipv6_mapped_result));
        REQUIRE_THAT(ipv6_mapped_result, Equals(ipv4_result));
        REQUIRE_THAT(ipv4_result, Equals(dummy.mac()));
        REQUIRE_THAT(ipv6_mapped_result, Equals(dummy.mac()));
    }
    
    SECTION("Multiple IPv4-mapped IPv6 formats") {
        DummyInterface dummy("test_formats", "10.0.0.100");
        
        std::vector<std::pair<std::string, std::string>> format_pairs = {
            {"10.0.0.100", "::ffff:10.0.0.100"},
            {"10.0.0.100", "::ffff:0a00:0064"},
        };
        
        for (const auto& [ipv4, ipv6_mapped] : format_pairs) {
            DYNAMIC_SECTION("Testing " << ipv4 << " vs " << ipv6_mapped) {
                auto ipv4_result = get_mac_address(ipv4);
                auto ipv6_result = get_mac_address(ipv6_mapped);
                
                REQUIRE(is_valid_mac_format(ipv4_result));
                REQUIRE(is_valid_mac_format(ipv6_result));
                REQUIRE_THAT(ipv6_result, Equals(ipv4_result));
                REQUIRE_THAT(ipv4_result, Equals(dummy.mac()));
                REQUIRE_THAT(ipv6_result, Equals(dummy.mac()));
            }
        }
    }
    
    SECTION("Different interfaces should have different MAC addresses") {
        DummyInterface dummy1("test_diff1", "192.168.101.1");
        DummyInterface dummy2("test_diff2", "192.168.102.1");
        
        auto mac1 = get_mac_address("192.168.101.1");
        auto mac2 = get_mac_address("192.168.102.1");
        
        REQUIRE(is_valid_mac_format(mac1));
        REQUIRE(is_valid_mac_format(mac2));
        REQUIRE_THAT(mac1, Equals(dummy1.mac()));
        REQUIRE_THAT(mac2, Equals(dummy2.mac()));
        REQUIRE_FALSE(dummy1.mac() == dummy2.mac());
    }
    
    SECTION("Interface with both IPv4 and IPv6 addresses") {
        DummyInterface dummy("test_dual", "192.168.103.1");
        REQUIRE(dummy.add_ipv6_address("2001:db8::100"));
        
        auto ipv4_mac = get_mac_address("192.168.103.1");
        auto ipv6_mac = get_mac_address("2001:db8::100");
        
        REQUIRE(is_valid_mac_format(ipv4_mac));
        REQUIRE(is_valid_mac_format(ipv6_mac));
        REQUIRE_THAT(ipv4_mac, Equals(dummy.mac()));
        REQUIRE_THAT(ipv6_mac, Equals(dummy.mac()));
        REQUIRE_THAT(ipv6_mac, Equals(ipv4_mac));
    }
}

TEST_CASE("get_mac_address: Non-existent IP addresses", "[hw_linux]") {
    SECTION("Non-existent IPv4 addresses should return default MAC") {
        std::vector<std::string> non_existent_ipv4 = {
            "192.0.2.1",
            "203.0.113.1",
            "198.51.100.1",
            "172.16.254.254",
            "10.255.255.254",
        };
        
        for (const auto& ip : non_existent_ipv4) {
            DYNAMIC_SECTION("Testing non-existent IPv4: " << ip) {
                auto result = get_mac_address(ip);
                REQUIRE(is_default_mac(result));
            }
        }
    }
    
    SECTION("Non-existent IPv6 addresses should return default MAC") {
        std::vector<std::string> non_existent_ipv6 = {
            "2001:db8::2",
            "2001:db8:1::1",
            "fc00::1",
            "fd12:3456:789a::1",
            "2001:0db8:85a3::8a2e:0370:7334",
        };
        
        for (const auto& ip : non_existent_ipv6) {
            DYNAMIC_SECTION("Testing non-existent IPv6: " << ip) {
                auto result = get_mac_address(ip);
                REQUIRE(is_default_mac(result));
            }
        }
    }
    
    SECTION("Non-existent IPv4-mapped IPv6 addresses should return default MAC") {
        std::vector<std::string> non_existent_mapped = {
            "::ffff:192.0.2.1",
            "::ffff:203.0.113.1",
            "::ffff:198.51.100.1",
            "::ffff:c000:0201",
        };
        
        for (const auto& ip : non_existent_mapped) {
            DYNAMIC_SECTION("Testing non-existent IPv4-mapped IPv6: " << ip) {
                auto result = get_mac_address(ip);
                REQUIRE(is_default_mac(result));
            }
        }
    }
}

TEST_CASE("get_mac_address: Malformed IP addresses", "[hw_linux]") {
    
    SECTION("Malformed addresses should return default MAC") {
        std::vector<std::string> malformed = {
            "192.168.1.1:8080",
            "http://192.168.1.1",
            "192.168.1.1/24",
            "::ffff:999.999.999.999",
            "[2001:db8::1]",
            "2001:db8::1%eth0",
            "2001:db8::gggg",
            "",
            "not.an.ip",
        };
        
        for (const auto& ip : malformed) {
            DYNAMIC_SECTION("Testing malformed address: " << ip) {
                auto result = get_mac_address(ip);
                REQUIRE(is_valid_mac_format(result));
                REQUIRE(is_default_mac(result));
            }
        }
    }
}
