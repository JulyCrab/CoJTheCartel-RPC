#include "DiscordIpc.h"

#include "Log.h"

#include <array>
#include <sstream>
#include <vector>

namespace {
constexpr std::uint32_t kHandshake = 0;
constexpr std::uint32_t kFrame = 1;
constexpr std::uint32_t kClose = 2;
constexpr std::uint32_t kPing = 3;
constexpr std::uint32_t kPong = 4;

bool WriteAll(HANDLE handle, const void* data, DWORD size) {
    const auto* cursor = static_cast<const unsigned char*>(data);
    while (size) {
        DWORD written = 0;
        if (!WriteFile(handle, cursor, size, &written, nullptr) || !written) return false;
        cursor += written;
        size -= written;
    }
    return true;
}

bool ReadAll(HANDLE handle, void* data, DWORD size) {
    auto* cursor = static_cast<unsigned char*>(data);
    while (size) {
        DWORD read = 0;
        if (!ReadFile(handle, cursor, size, &read, nullptr) || !read) return false;
        cursor += read;
        size -= read;
    }
    return true;
}
}

DiscordIpc::~DiscordIpc() {
    Disconnect();
}

bool DiscordIpc::Connect(std::string_view applicationId) {
    Disconnect();
    constexpr int kMaxPipes = 10;
    for (int index = 0; index < kMaxPipes; ++index) {
        const std::string path = "\\\\?\\pipe\\discord-ipc-" + std::to_string(index);
        pipe_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe_ != INVALID_HANDLE_VALUE) break;
    }
    if (pipe_ == INVALID_HANDLE_VALUE) return false;

    const std::string handshake = "{\"v\":1,\"client_id\":\"" + Escape(applicationId) + "\"}";
    if (!WriteFrame(kHandshake, handshake)) {
        Disconnect();
        return false;
    }

    std::uint32_t opcode = 0;
    std::string response;
    if (!ReadFrame(opcode, response) || opcode == kClose) {
        Disconnect();
        return false;
    }
    Log("Connected to Discord IPC.");

    // Needed to receive ACTIVITY_JOIN when a friend presses Join
    const std::string subscribe = "{\"cmd\":\"SUBSCRIBE\",\"evt\":\"ACTIVITY_JOIN\",\"nonce\":\"" + std::to_string(nonce_++) + "\"}";
    WriteFrame(kFrame, subscribe);
    return true;
}

bool DiscordIpc::SetActivity(const DiscordActivity& activity) {
    if (!IsConnected()) return false;
    std::ostringstream json;
    json << "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" << GetCurrentProcessId() << ",\"activity\":{";
    bool comma = false;
    const auto field = [&](const char* key, const std::string& value) {
        if (value.empty()) return;
        if (comma) json << ',';
        json << '\"' << key << "\":\"" << Escape(value) << '\"';
        comma = true;
    };
    field("details", activity.details);
    field("state", activity.state);
    if (activity.startTimestamp > 0) {
        if (comma) json << ',';
        json << "\"timestamps\":{\"start\":" << activity.startTimestamp << '}';
        comma = true;
    }
    if (!activity.largeImage.empty()) {
        if (comma) json << ',';
        json << "\"assets\":{\"large_image\":\"" << Escape(activity.largeImage) << '\"';
        if (!activity.largeText.empty()) json << ",\"large_text\":\"" << Escape(activity.largeText) << '\"';
        if (!activity.smallImage.empty()) {
            json << ",\"small_image\":\"" << Escape(activity.smallImage) << '\"';
            if (!activity.smallText.empty()) json << ",\"small_text\":\"" << Escape(activity.smallText) << '\"';
        }
        json << '}';
        comma = true;
    }
    if (!activity.partyId.empty()) {
        if (comma) json << ',';
        json << "\"party\":{\"id\":\"" << Escape(activity.partyId) << '\"';
        if (activity.partyMax > 0 && activity.partySize > 0) {
            json << ",\"size\":[" << activity.partySize << ',' << activity.partyMax << ']';
        }
        json << '}';
        comma = true;
    }
    // Discord rejects the whole activity when it has both
    if (!activity.joinSecret.empty()) {
        if (comma) json << ',';
        json << "\"secrets\":{\"join\":\"" << Escape(activity.joinSecret) << "\"}";
    } else if (!activity.buttonLabel.empty() && !activity.buttonUrl.empty()) {
        if (comma) json << ',';
        json << "\"buttons\":[{\"label\":\"" << Escape(activity.buttonLabel)
             << "\",\"url\":\"" << Escape(activity.buttonUrl) << "\"}]";
    }
    json << "}},\"nonce\":\"" << nonce_++ << "\"}";
    if (!WriteFrame(kFrame, json.str())) {
        Disconnect();
        return false;
    }
    return true;
}

std::string DiscordIpc::Pump() {
    std::string joinSecret;
    if (!IsConnected()) return joinSecret;
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr)) {
            Disconnect();
            return joinSecret;
        }
        if (available < 8) return joinSecret;
        std::uint32_t opcode = 0;
        std::string json;
        if (!ReadFrame(opcode, json)) {
            Disconnect();
            return joinSecret;
        }
        if (opcode == kPing) WriteFrame(kPong, json);
        if (opcode == kClose) {
            Disconnect();
            return joinSecret;
        }
        if (opcode != kFrame) continue;
        const std::string evt = ExtractString(json, "evt");
        if (evt == "ACTIVITY_JOIN") {
            joinSecret = ExtractString(json, "secret");
        } else if (evt == "ERROR") {
            Log("Discord error: %s", ExtractString(json, "message").c_str());
        }
    }
}

void DiscordIpc::Disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool DiscordIpc::WriteFrame(std::uint32_t opcode, std::string_view json) {
    constexpr size_t kMaxPayloadSize = 1024 * 1024;
    if (!IsConnected() || json.size() > kMaxPayloadSize) return false;
    const std::array<std::uint32_t, 2> header{opcode, static_cast<std::uint32_t>(json.size())};
    return WriteAll(pipe_, header.data(), static_cast<DWORD>(sizeof(header))) &&
           WriteAll(pipe_, json.data(), static_cast<DWORD>(json.size()));
}

bool DiscordIpc::ReadFrame(std::uint32_t& opcode, std::string& json) {
    constexpr size_t kMaxPayloadSize = 1024 * 1024;
    std::array<std::uint32_t, 2> header{};
    if (!ReadAll(pipe_, header.data(), static_cast<DWORD>(sizeof(header)))) return false;
    opcode = header[0];
    if (header[1] > kMaxPayloadSize) return false;
    std::vector<char> payload(header[1]);
    if (header[1] && !ReadAll(pipe_, payload.data(), header[1])) return false;
    json.assign(payload.begin(), payload.end());
    return true;
}

// Good enough for the few string fields we need from Discord's replies
std::string DiscordIpc::ExtractString(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos || json[pos] != '"') return "";

    std::string result;
    for (++pos; pos < json.size() && json[pos] != '"'; ++pos) {
        if (json[pos] == '\\' && pos + 1 < json.size()) ++pos;
        result += json[pos];
    }
    return result;
}

std::string DiscordIpc::Escape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch >= 0x20) result += static_cast<char>(ch);
            break;
        }
    }
    return result;
}
