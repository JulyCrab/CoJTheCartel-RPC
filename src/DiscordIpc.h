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
    std::string buttonLabel;
    std::string buttonUrl;
    std::int64_t startTimestamp = 0;
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
    void Pump();
    void Disconnect();
    bool IsConnected() const { return pipe_ != INVALID_HANDLE_VALUE; }

private:
    bool WriteFrame(std::uint32_t opcode, std::string_view json);
    bool ReadFrame(std::uint32_t& opcode, std::string& json);
    static std::string Escape(std::string_view value);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::uint64_t nonce_ = 1;
};

