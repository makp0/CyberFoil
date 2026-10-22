#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "util/json.hpp"

namespace inst::inventory {
    struct InstalledTitle {
        std::uint64_t titleId;
        std::uint32_t version;
    };

    inline bool ValidDeviceId(const std::string& id) {
        return id.size() >= 32 && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
    }

    inline bool Supported(const nlohmann::json& body, const std::string& url,
                          const std::string& user, const std::string& pass, bool legacy) {
        if (legacy || url.rfind("https://", 0) != 0 || user.empty() || pass.empty() || !body.is_object())
            return false;
        const auto caps = body.find("capabilities");
        if (caps == body.end() || !caps->is_object()) return false;
        const auto version = caps->find("console_inventory");
        return version != caps->end() && version->is_number_integer() && *version == 1;
    }

    inline std::string Payload(const std::string& deviceId, const std::vector<InstalledTitle>& installed) {
        if (!ValidDeviceId(deviceId)) throw std::invalid_argument("Invalid inventory identity");
        std::map<std::uint64_t, std::uint32_t> titles;
        for (const auto& item : installed) {
            // Only application base IDs: never report system titles or patch/DLC rows as games.
            if ((item.titleId >> 52) != 0x010 || item.titleId < 0x0100000000010000 || (item.titleId & 0xfff) != 0) continue;
            auto [it, added] = titles.emplace(item.titleId, item.version);
            if (!added) it->second = std::max(it->second, item.version);
        }
        if (titles.size() > 5000) throw std::length_error("Inventory exceeds supported size");
        auto rows = nlohmann::json::array();
        for (const auto& [id, version] : titles) {
            char text[17];
            static constexpr char hex[] = "0123456789ABCDEF";
            for (int i = 0; i < 16; ++i) text[i] = hex[(id >> ((15 - i) * 4)) & 15];
            text[16] = 0;
            rows.push_back({{"title_id", text}, {"installed_version", version}});
        }
        return nlohmann::json({{"schema", 1}, {"device_id", deviceId}, {"device_name", "Switch"}, {"titles", rows}}).dump();
    }
}
