#pragma once

// XUNA Direct Device Transport (CLD-40, CLD-41, E14, H1-H8).
// Bounded, deterministic C++11 MQTT topic and payload helpers.
// No dynamic heap allocation to preserve ESP32-S3 SRAM budget.

#include <cstdio>
#include <cstring>
#include <cstdint>

namespace xuna {
namespace transport {

// Maximum lengths for safe bounded static buffers
static const size_t kMaxTopicLen = 128;
static const size_t kMaxPayloadLen = 512;

inline bool buildStatusTopic(char* out, size_t outLen, const char* deviceId) {
    if (!out || !deviceId || outLen < 32) return false;
    int written = std::snprintf(out, outLen, "xuna/v1/%s/status", deviceId);
    return written > 0 && static_cast<size_t>(written) < outLen;
}

inline bool buildEventsTopic(char* out, size_t outLen, const char* deviceId) {
    if (!out || !deviceId || outLen < 32) return false;
    int written = std::snprintf(out, outLen, "xuna/v1/%s/up/events", deviceId);
    return written > 0 && static_cast<size_t>(written) < outLen;
}

inline bool buildStateTopic(char* out, size_t outLen, const char* deviceId) {
    if (!out || !deviceId || outLen < 32) return false;
    int written = std::snprintf(out, outLen, "xuna/v1/%s/up/state", deviceId);
    return written > 0 && static_cast<size_t>(written) < outLen;
}

inline bool buildCmdTopic(char* out, size_t outLen, const char* deviceId) {
    if (!out || !deviceId || outLen < 32) return false;
    int written = std::snprintf(out, outLen, "xuna/v1/%s/down/cmd", deviceId);
    return written > 0 && static_cast<size_t>(written) < outLen;
}

inline bool buildSuggestTopic(char* out, size_t outLen, const char* deviceId) {
    if (!out || !deviceId || outLen < 32) return false;
    int written = std::snprintf(out, outLen, "xuna/v1/%s/down/suggest", deviceId);
    return written > 0 && static_cast<size_t>(written) < outLen;
}

inline bool buildStatusPayload(char* out, size_t outLen, const char* status) {
    if (!out || !status || outLen < 32) return false;
    int written = std::snprintf(out, outLen, "{\"status\":\"%s\"}", status);
    return written > 0 && static_cast<size_t>(written) < outLen;
}

inline bool buildEventPayload(char* out, size_t outLen, const char* eventType, uint32_t timestampMs) {
    if (!out || !eventType || outLen < 64) return false;
    int written = std::snprintf(out, outLen,
        "{\"eventType\":\"%s\",\"timestampMs\":%u}",
        eventType, static_cast<unsigned int>(timestampMs));
    return written > 0 && static_cast<size_t>(written) < outLen;
}

inline bool buildStatePayload(char* out, size_t outLen, uint32_t generation, uint32_t activeSeconds, uint16_t feedbackCount) {
    if (!out || outLen < 96) return false;
    int written = std::snprintf(out, outLen,
        "{\"snapshotGeneration\":%u,\"life\":{\"activeSeconds\":%u,\"feedbackCount\":%u}}",
        static_cast<unsigned int>(generation),
        static_cast<unsigned int>(activeSeconds),
        static_cast<unsigned int>(feedbackCount));
    return written > 0 && static_cast<size_t>(written) < outLen;
}

} // namespace transport
} // namespace xuna
