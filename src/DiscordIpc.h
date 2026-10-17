#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <memory>

struct DiscordActivity {
    std::string details;
    std::string state;
    std::string largeImage;
    std::string largeText;
    std::string smallImage;
    std::string smallText;
    std::string buttonLabel;
    std::string buttonUrl;
    std::int64_t startTimestamp = 0;

    std::string partyId;
    int partySize = 0;
    int partyMax = 0;
    std::string joinSecret; // Discord drops buttons when this is set

    bool operator==(const DiscordActivity& o) const {
        return details == o.details && state == o.state && largeImage == o.largeImage &&
               largeText == o.largeText && smallImage == o.smallImage && smallText == o.smallText &&
               buttonLabel == o.buttonLabel && buttonUrl == o.buttonUrl && startTimestamp == o.startTimestamp &&
               partyId == o.partyId && partySize == o.partySize && partyMax == o.partyMax &&
               joinSecret == o.joinSecret;
    }
    bool operator!=(const DiscordActivity& o) const { return !(*this == o); }
};

class DiscordIpc {
public:
    DiscordIpc() = default;
    ~DiscordIpc();
    DiscordIpc(const DiscordIpc&) = delete;
    DiscordIpc& operator=(const DiscordIpc&) = delete;
    DiscordIpc(DiscordIpc&&) = delete;
    DiscordIpc& operator=(DiscordIpc&&) = delete;

    bool Connect(std::string_view applicationId);
    bool SetActivity(const DiscordActivity& activity);
    // Returns the join secret when a friend pressed Join on our presence
    std::string Pump();
    void Disconnect();
    bool IsConnected() const { return pipe_ != INVALID_HANDLE_VALUE; }

private:
    bool WriteFrame(std::uint32_t opcode, std::string_view json);
    bool ReadFrame(std::uint32_t& opcode, std::string& json);
    static std::string Escape(std::string_view value);
    static std::string ExtractString(const std::string& json, const char* key);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::uint64_t nonce_ = 1;
};
