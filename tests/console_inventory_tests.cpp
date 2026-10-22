#include "util/console_inventory.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace inst::inventory;
    const std::string identity(32, 'a');
    const nlohmann::json advertised = {{"capabilities", {{"console_inventory", 1}}}};
    assert(Supported(advertised, "https://shop.example", "user", "pass", false));
    assert(!Supported(advertised, "http://shop.example", "user", "pass", false));
    assert(!Supported(advertised, "https://shop.example", "user", "pass", true));
    assert(!Supported(advertised, "https://shop.example", "", "", false));
    for (const nlohmann::json& body : {nlohmann::json::object(), nlohmann::json({{"capabilities", true}}),
            nlohmann::json({{"capabilities", {{"console_inventory", "1"}}}}),
            nlohmann::json({{"capabilities", {{"console_inventory", true}}}}),
            nlohmann::json({{"capabilities", {{"console_inventory", 2}}}})})
        assert(!Supported(body, "https://shop.example", "user", "pass", false));
    assert(!ValidDeviceId("MAC:11:22"));
    const auto body = nlohmann::json::parse(Payload(identity, {
        {0x0100000000010000, 0}, {0x0100000000010000, 131072},
        {0x0100000000010800, 65536}, {0x0100000000011001, 0}, {0x0100000000020000, UINT32_MAX},
        {0x0100000000030000, 0}, {0x0100000000030000, 0}, {0x0100000000000001, 0}, {0x0100000000000010, 0},
        {0x0100000000001000, 0}, {0x0200000000010000, 0}, {0x0110000000010000, 0}, {0x01F0000000010000, 0}}));
    assert(body["schema"] == 1 && body["device_id"] == identity);
    assert(body["titles"].size() == 3);
    bool merged = false, baseOnly = false, maximum = false;
    for (const auto& title : body["titles"]) {
        if (title["title_id"] == "0100000000010000") merged = title["installed_version"] == 131072;
        if (title["title_id"] == "0100000000030000") baseOnly = title["installed_version"] == 0;
        if (title["title_id"] == "0100000000020000") maximum = title["installed_version"] == UINT32_MAX;
    }
    assert(merged && baseOnly && maximum);
    assert(nlohmann::json::parse(Payload(identity, {}))["titles"].empty()); // An empty successful scan clears server inventory.
    bool rejected = false;
    try { Payload("bad", {}); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::vector<InstalledTitle> oversized;
    for (std::uint64_t i = 0; i < 5001; ++i) oversized.push_back({0x0100000000010000 + i * 0x1000, 0});
    rejected = false;
    try { Payload(identity, oversized); } catch (const std::length_error&) { rejected = true; }
    assert(rejected); // Never silently truncate a full snapshot.
    std::cout << "Console inventory protocol tests passed\n";
}
