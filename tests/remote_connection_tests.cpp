#include "util/config.hpp"
#include "util/remote_connection.hpp"
#include "util/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace cfg = inst::config;
static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void EndpointTests() {
    std::string url;
    Check(cfg::NormalizeRemoteEndpoint("http://192.168.1.2:8465/remote/", url) &&
        url == "http://192.168.1.2:8465/remote", "LAN URL and independent path");
    Check(cfg::NormalizeRemoteEndpoint("https://shop.example.com", url) &&
        url == "https://shop.example.com:443", "standard HTTPS port");
    Check(cfg::NormalizeRemoteEndpoint("http://shop.example.com", url) &&
        url == "http://shop.example.com:80", "standard HTTP port");
    Check(cfg::NormalizeRemoteEndpoint("https://shop.example.com:9443/shop", url) &&
        url == "https://shop.example.com:9443/shop", "explicit WAN port/path");
    Check(cfg::NormalizeRemoteEndpoint("http://[::1]:8465", url), "IPv6 endpoint");
    Check(cfg::NormalizeRemoteEndpoint("https://shop.example.com/shop@home", url), "at sign in path is not embedded credentials");
    Check(cfg::NormalizeRemoteEndpoint("", url) && url.empty(), "optional endpoint");
    Check(cfg::NormalizeRemoteAddress("192.168.1.2:8465", "http", url) && url == "http://192.168.1.2:8465",
        "schemeless LAN HTTP address");
    Check(cfg::NormalizeRemoteAddress("192.168.1.2", "https", url) && url == "https://192.168.1.2:443",
        "LAN HTTPS selection and default port");
    Check(cfg::NormalizeRemoteAddress("shop.example.com:9443/shop", "https", url) && url == "https://shop.example.com:9443/shop",
        "Remote HTTPS added automatically");
    Check(cfg::NormalizeRemoteAddress("https://shop.example.com", "https", url), "matching pasted scheme accepted");
    Check(!cfg::NormalizeRemoteAddress("http://shop.example.com", "https", url), "Remote HTTP rejected");
    Check(!cfg::NormalizeRemoteAddress("https://lan.example", "http", url), "LAN scheme cannot override dropdown");
    Check(!cfg::NormalizeRemoteAddress("shop.example.com", "ftp", url), "unsupported selected protocol rejected");
    for (const char* invalid : {"ftp://shop.example.com", "shop.example.com", "http://", "http://host:",
            "http://host:0", "http://host:65536", "http://host:abc", "http://user:pass@host", "http://host?q=1",
            "http://host#fragment", "http://[abc]", "http://[::1]oops", "http://bad host", "http://host\\path"})
        Check(!cfg::NormalizeRemoteEndpoint(invalid, url), "invalid endpoint");

    cfg::RemoteProfile profile;
    profile.wanUrl = "http://shop.example.com:80";
    Check(!cfg::NormalizeRemoteProfile(profile), "saved Remote endpoint requires HTTPS");
    profile = {};
    profile.lanUrl = "http://192.168.1.2:8465";
    profile.wanUrl = "https://shop.example.com:443/shop";
    Check(cfg::NormalizeRemoteProfile(profile), "dual endpoint normalization");
    int probes = 0;
    auto online = [&](const std::string& candidate) {
        ++probes; Check(candidate == profile.lanUrl, "only LAN is probed"); return true;
    };
    Check(inst::remote::ChooseEndpoint(profile, online) == profile.lanUrl && probes == 1, "Auto LAN first");
    Check(inst::remote::ChooseEndpoint(profile, [](const std::string&) { return false; }) == profile.wanUrl,
        "Auto WAN fallback for unreachable LAN");
    for (const std::string mode : {"lan", "wan"}) {
        profile.connection = mode;
        probes = 0;
        Check(inst::remote::ChooseEndpoint(profile, online) == (mode == "lan" ? profile.lanUrl : profile.wanUrl)
            && probes == 0, "forced connection skips probe/fallback");
    }
    Check(cfg::RemoteMatchesUrl(profile, profile.lanUrl) && cfg::RemoteMatchesUrl(profile, profile.wanUrl),
        "active profile matches either session address");
    profile.connection = "auto";
    profile.wanUrl.clear(); probes = 0;
    Check(inst::remote::ChooseEndpoint(profile, online) == profile.lanUrl && probes == 0, "single endpoint keeps existing behavior");
    profile.connection = "wan"; Check(!cfg::NormalizeRemoteProfile(profile), "missing forced WAN URL");
    profile.connection = "invalid"; Check(!cfg::NormalizeRemoteProfile(profile), "invalid policy");
    profile = {}; profile.wanUrl = "https://shop.example.com:443"; probes = 0;
    Check(cfg::NormalizeRemoteProfile(profile) && inst::remote::ChooseEndpoint(profile, online) == profile.wanUrl && probes == 0,
        "WAN-only Auto profile does not probe LAN");
    profile.connection = "lan"; Check(!cfg::NormalizeRemoteProfile(profile), "missing forced LAN URL");
    profile = {}; Check(!cfg::NormalizeRemoteProfile(profile), "both endpoints missing");
    profile.host = "192.168.1.8"; profile.protocol = "http"; profile.port = 8465; profile.path = "/base";
    profile.username = "user"; profile.password = "password";
    Check(cfg::NormalizeRemoteProfile(profile) && profile.lanUrl == "http://192.168.1.8:8465/base" &&
        profile.wanUrl.empty() && profile.username == "user" && profile.password == "password", "legacy URL/credentials migration");
    profile = {}; profile.protocol = "https"; profile.host = "shop.example.com"; profile.port = 443; profile.path = "/remote";
    Check(cfg::NormalizeRemoteProfile(profile) && profile.lanUrl.empty() && profile.wanUrl == "https://shop.example.com:443/remote",
        "legacy public HTTPS endpoint belongs in Remote URL");
    for (const char* host : {"10.1.2.3", "172.16.0.9", "192.168.1.8", "169.254.1.8", "127.0.0.1",
            "localhost", "aerofoil", "aerofoil.local", "[::1]", "[fd00::1]", "[fe80::1]", "[::ffff:192.168.1.8]"}) {
        profile = {}; profile.protocol = "https"; profile.host = host; profile.port = 443;
        Check(cfg::NormalizeRemoteProfile(profile) && !profile.lanUrl.empty() && profile.wanUrl.empty(),
            "legacy private/local HTTPS endpoint remains LAN");
    }
}

static void PersistenceTests() {
    // POSIX hosts allow the test's relative sdmc: tree inside this unique temp
    // directory. Real console data and the repository are never touched.
    const auto previous = std::filesystem::current_path();
    const auto scratch = std::filesystem::temp_directory_path() /
        ("cyberfoil-remote-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(scratch);
    struct Cleanup {
        std::filesystem::path previous, scratch;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::current_path(previous, ec);
            std::filesystem::remove_all(scratch, ec);
        }
    } cleanup{previous, scratch};
    std::filesystem::current_path(scratch);
    cfg::RemoteProfile profile;
    profile.title = "Dual endpoint";
    profile.lanUrl = "http://192.168.1.9:8465";
    profile.wanUrl = "https://shop.example.com:9443/shop";
    profile.uid = "12345678-1234-4abc-8def-123456789abc";
    profile.username = "account"; profile.password = "secret";
    profile.connection = "wan";
    Check(cfg::SaveRemote(profile), "save dual endpoint profile");
    auto remotes = cfg::LoadRemotes();
    Check(remotes.size() == 1, "load saved profile");
    const auto& loaded = remotes.front();
    Check(loaded.lanUrl == profile.lanUrl && loaded.wanUrl == profile.wanUrl && loaded.uid == profile.uid &&
        loaded.connection == "wan" && loaded.username == "account" && loaded.password == "secret", "JSON round trip");
    cfg::httpUserAgentMode = "custom"; cfg::httpUserAgent = "my-agent";
    Check(cfg::SetActiveRemote(profile, false) && cfg::remoteUrl == profile.wanUrl &&
        cfg::remoteProfileFile == loaded.fileName && cfg::remoteUser == "account" && cfg::remotePass == "secret" &&
        cfg::httpUserAgent == "my-agent", "activation preserves shared credentials/UA and stores profile identity");
    std::ifstream saved(cfg::remotesDir + "/" + loaded.fileName);
    nlohmann::json savedJson; saved >> savedJson;
    Check(savedJson["remote"]["host"] == "shop.example.com" && savedJson["remote"]["port"] == 9443,
        "compatibility fields use preferred endpoint");
    saved.close();
    nlohmann::json legacy = {{"remote", {{"protocol", "http"}, {"host", "192.168.1.3"}, {"port", 8465},
        {"path", "/remote"}, {"title", "Old profile"}, {"username", "old-user"}, {"password", "old-password"}}}};
    std::ofstream old(cfg::remotesDir + "/legacy.json"); old << legacy; old.close();
    remotes = cfg::LoadRemotes();
    bool migrated = false;
    for (const auto& remote : remotes) if (remote.fileName == "legacy.json") {
        migrated = remote.lanUrl == "http://192.168.1.3:8465/remote" && remote.wanUrl.empty() &&
            remote.username == "old-user" && remote.password == "old-password";
    }
    Check(migrated, "old JSON profile migration");
    nlohmann::json malformed = {{"remote", {{"title", "Bad"}, {"lanUrl", 123}, {"wanUrl", ""}}}};
    std::ofstream bad(cfg::remotesDir + "/malformed.json"); bad << malformed; bad.close();
    Check(cfg::LoadRemotes().size() == 2, "malformed endpoint field ignored safely");

    cfg::RemoteProfile active;
    for (const auto& remote : cfg::LoadRemotes()) if (remote.uid == profile.uid) active = remote;
    Check(!active.fileName.empty(), "find original saved profile");
    active.connection = "auto";
    Check(cfg::SaveRemote(active) && cfg::SetActiveRemote(active, false), "activate dual endpoint Auto profile");
    cfg::remoteUrl = active.wanUrl; // Runtime connection selected WAN.
    cfg::inventoryDeviceId = std::string(32, 'a');
    cfg::setConfig();
    std::ifstream configFile(cfg::configPath);
    nlohmann::json configJson; configFile >> configJson; configFile.close();
    Check(configJson["remoteUrl"] == active.lanUrl && configJson["remoteProfileFile"] == active.fileName,
        "runtime WAN selection does not overwrite persisted profile preference");
    for (int i = 0; i < 3; ++i) {
        cfg::parseConfig();
        Check(cfg::inventoryDeviceId == std::string(32, 'a'), "inventory identity survives repeated startup");
        Check(cfg::LoadRemotes().size() == 2 && cfg::remoteProfileFile == active.fileName,
            "repeated startup does not generate remote duplicates");
    }
    cfg::remoteProfileFile.clear();
    cfg::remoteUrl = "https://unsaved.example.com:443";
    cfg::remoteUser = "different-account";
    cfg::setConfig();
    cfg::parseConfig();
    Check(cfg::LoadRemotes().size() == 2, "modern standalone config URL does not auto-create a profile");
    nlohmann::json oldConfig = {{"remoteUrl", "https://legacy.example.com:443/remote"},
        {"remoteUser", "legacy-user"}, {"remotePass", "legacy-password"}, {"remoteLegacyMode", true}};
    std::ofstream legacyConfig(cfg::configPath); legacyConfig << oldConfig; legacyConfig.close();
    cfg::parseConfig();
    Check(cfg::LoadRemotes().size() == 3, "genuine legacy config imports one profile");
    bool importedWan = false;
    for (const auto& remote : cfg::LoadRemotes()) if (remote.username == "legacy-user")
        importedWan = remote.lanUrl.empty() && remote.wanUrl == "https://legacy.example.com:443/remote" && remote.legacyMode;
    Check(importedWan, "legacy WAN import preserves Remote role, credentials, and mode");
    cfg::parseConfig();
    Check(cfg::LoadRemotes().size() == 3, "legacy config import runs only once");
}
int main() {
    try { EndpointTests(); PersistenceTests(); std::cout << "Remote endpoint tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
