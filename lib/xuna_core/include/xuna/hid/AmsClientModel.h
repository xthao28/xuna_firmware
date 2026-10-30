#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <xuna/hid/ConsumerReport.h>

namespace xuna {
namespace hid {

// Apple Media Service (AMS) specification identifiers (UDC-6 §4.3).
// Reference: Apple Media Service Reference (CoreBluetooth).
//
// Service UUID:            89D3502B-0F36-433A-8EF4-C502AD55F8DC
// Remote Command UUID:     9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2
// Entity Update UUID:      2F7CABCE-808D-411F-9A0C-BB92BA96C102
// Entity Attribute UUID:   C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7

enum class AmsRemoteCommand : uint8_t {
  Play = 0,
  Pause = 1,
  TogglePlayPause = 2,
  NextTrack = 3,
  PreviousTrack = 4,
  VolumeUp = 5,
  VolumeDown = 6,
  None = 0xFF
};

enum class AmsEntityId : uint8_t {
  Player = 0,
  Queue = 1,
  Track = 2
};

enum class AmsPlayerAttributeId : uint8_t {
  Name = 0,
  PlaybackInfo = 1,
  Volume = 2
};

enum class AmsTrackAttributeId : uint8_t {
  Artist = 0,
  Album = 1,
  Title = 2,
  Duration = 3
};

class AmsClientModel {
 public:
  static constexpr size_t kMaxFieldBytes = 72;
  static constexpr size_t kMaxDisplayChars = 24;
  static constexpr uint8_t kFlagTruncated = 0x01;

  static const char* serviceUuidStr() {
    return "89D3502B-0F36-433A-8EF4-C502AD55F8DC";
  }
  static const char* remoteCommandUuidStr() {
    return "9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2";
  }
  static const char* entityUpdateUuidStr() {
    return "2F7CABCE-808D-411F-9A0C-BB92BA96C102";
  }
  static const char* entityAttributeUuidStr() {
    return "C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7";
  }

  // Little-endian 128-bit UUID bytes matching Bluedroid esp_bt_uuid_t::uuid128.
  static const uint8_t* serviceUuidLe() {
    static const uint8_t kBytes[16] = {
        0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E,
        0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89};
    return kBytes;
  }

  static const uint8_t* remoteCommandUuidLe() {
    static const uint8_t kBytes[16] = {
        0xC2, 0x51, 0xCA, 0xF7, 0x56, 0x0E, 0xDF, 0xB8,
        0x8A, 0x4A, 0xB1, 0x57, 0xD8, 0x81, 0x3C, 0x9B};
    return kBytes;
  }

  static const uint8_t* entityUpdateUuidLe() {
    static const uint8_t kBytes[16] = {
        0x02, 0xC1, 0x96, 0xBA, 0x92, 0xBB, 0x0C, 0x9A,
        0x1F, 0x41, 0x8D, 0x80, 0xCE, 0xAB, 0x7C, 0x2F};
    return kBytes;
  }

  static const uint8_t* entityAttributeUuidLe() {
    static const uint8_t kBytes[16] = {
        0xD7, 0xD5, 0xBB, 0x70, 0xA8, 0xA3, 0xAB, 0xA6,
        0xD8, 0x46, 0xAB, 0x23, 0x8C, 0xF3, 0xB2, 0xC6};
    return kBytes;
  }

  // Entity Update write payload: [EntityIDTrack (2), Title (2), Artist (0)].
  static const uint8_t* trackSubscriptionPayload(size_t& outLen) {
    static const uint8_t kPayload[3] = {
        static_cast<uint8_t>(AmsEntityId::Track),
        static_cast<uint8_t>(AmsTrackAttributeId::Title),
        static_cast<uint8_t>(AmsTrackAttributeId::Artist)};
    outLen = sizeof(kPayload);
    return kPayload;
  }

  // Entity Update write payload: [EntityIDPlayer (0), PlaybackInfo (1)].
  static const uint8_t* playerSubscriptionPayload(size_t& outLen) {
    static const uint8_t kPayload[2] = {
        static_cast<uint8_t>(AmsEntityId::Player),
        static_cast<uint8_t>(AmsPlayerAttributeId::PlaybackInfo)};
    outLen = sizeof(kPayload);
    return kPayload;
  }

  // Maps XUNA ConsumerKey to AMS RemoteCommandID (UDC-6 §4.3).
  static AmsRemoteCommand mapConsumerKey(ConsumerKey key) {
    switch (key) {
      case ConsumerKey::PlayPause:
        return AmsRemoteCommand::TogglePlayPause;
      case ConsumerKey::Next:
        return AmsRemoteCommand::NextTrack;
      case ConsumerKey::Previous:
        return AmsRemoteCommand::PreviousTrack;
      case ConsumerKey::VolumeUp:
        return AmsRemoteCommand::VolumeUp;
      case ConsumerKey::VolumeDown:
        return AmsRemoteCommand::VolumeDown;
      case ConsumerKey::Stop:
        return AmsRemoteCommand::Pause;
      case ConsumerKey::Mute:
      case ConsumerKey::None:
        return AmsRemoteCommand::None;
    }
    return AmsRemoteCommand::None;
  }

  void reset() {
    ready_ = false;
    playing_ = false;
    trackChanged_ = false;
    supportedCommandsMask_ = 0x7F;  // Default all 0..6 until Remote Command notifies
    title_[0] = '\0';
    artist_[0] = '\0';
  }

  void setReady(bool ready) {
    ready_ = ready;
    if (!ready) {
      playing_ = false;
      trackChanged_ = false;
      title_[0] = '\0';
      artist_[0] = '\0';
    }
  }

  bool ready() const { return ready_; }
  bool playing() const { return playing_; }
  uint8_t supportedCommandsMask() const { return supportedCommandsMask_; }
  const char* title() const { return title_; }
  const char* artist() const { return artist_; }
  bool hasTrackInfo() const { return title_[0] != '\0' || artist_[0] != '\0'; }

  bool takeTrackChanged() {
    if (!trackChanged_) return false;
    trackChanged_ = false;
    return true;
  }

  bool isCommandSupported(AmsRemoteCommand cmd) const {
    const uint8_t id = static_cast<uint8_t>(cmd);
    if (id > 6) return false;
    return (supportedCommandsMask_ & uint8_t(1u << id)) != 0;
  }

  // Parses Remote Command notification containing 1-byte RemoteCommandIDs.
  void onRemoteCommandNotify(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
      supportedCommandsMask_ = 0;
      return;
    }
    uint8_t mask = 0;
    for (size_t i = 0; i < len; ++i) {
      if (data[i] <= 6) {
        mask |= uint8_t(1u << data[i]);
      }
    }
    supportedCommandsMask_ = mask;
  }

  // Parses Entity Update notification: [EntityID, AttributeID, Flags, Value...].
  // Returns true if track title/artist or playback state changed.
  bool onEntityUpdateNotify(const uint8_t* data, size_t len) {
    if (!data || len < 3) return false;
    const uint8_t entityId = data[0];
    const uint8_t attrId = data[1];
    const uint8_t* val = data + 3;
    const size_t valLen = len - 3;

    if (entityId == static_cast<uint8_t>(AmsEntityId::Player) &&
        attrId == static_cast<uint8_t>(AmsPlayerAttributeId::PlaybackInfo)) {
      // Format: "PlaybackState,PlaybackRate,ElapsedTime" where state '1' = Playing.
      const bool nowPlaying = (valLen >= 1 && val[0] == '1');
      const bool changed = (playing_ != nowPlaying);
      playing_ = nowPlaying;
      return changed;
    }

    if (entityId == static_cast<uint8_t>(AmsEntityId::Track)) {
      if (attrId == static_cast<uint8_t>(AmsTrackAttributeId::Title)) {
        if (copySanitizedUtf8(val, valLen, title_, sizeof(title_))) {
          trackChanged_ = true;
          return true;
        }
      } else if (attrId == static_cast<uint8_t>(AmsTrackAttributeId::Artist)) {
        if (copySanitizedUtf8(val, valLen, artist_, sizeof(artist_))) {
          trackChanged_ = true;
          return true;
        }
      }
    }
    return false;
  }

  // Formats the truncated track string (<= 24 UTF-8 code points) in `out`.
  // Kept in RAM only, never sent anywhere (UDC-6 §4.3).
  void formatTrackSummary24(char* out, size_t cap) const {
    if (!out || cap == 0) return;
    out[0] = '\0';
    char combined[kMaxFieldBytes * 2 + 8] = {};
    if (title_[0] != '\0' && artist_[0] != '\0') {
      std::snprintf(combined, sizeof(combined), "%s - %s", title_, artist_);
    } else if (title_[0] != '\0') {
      std::snprintf(combined, sizeof(combined), "%s", title_);
    } else if (artist_[0] != '\0') {
      std::snprintf(combined, sizeof(combined), "%s", artist_);
    } else {
      return;
    }
    truncateUtf8CodePoints(combined, kMaxDisplayChars, out, cap);
  }

  // Formats the Remote bubble text: shows `"Remote > <track 24 chars>"` when
  // AMS track metadata is available, or `"Remote > máy <slotNum>"` otherwise.
  void formatRemoteBubble(unsigned slotNum, char* out, size_t cap) const {
    if (!out || cap == 0) return;
    if (ready_ && hasTrackInfo()) {
      char track24[kMaxFieldBytes] = {};
      formatTrackSummary24(track24, sizeof(track24));
      if (track24[0] != '\0') {
        std::snprintf(out, cap, "Remote > %s", track24);
        return;
      }
    }
    std::snprintf(out, cap, "Remote > máy %u", slotNum);
  }

  // Truncates a UTF-8 string to at most `maxChars` Unicode code points without
  // splitting multi-byte UTF-8 sequences.
  static size_t truncateUtf8CodePoints(const char* src, size_t maxChars,
                                       char* dst, size_t dstCap) {
    if (!dst || dstCap == 0) return 0;
    dst[0] = '\0';
    if (!src) return 0;

    size_t srcIdx = 0;
    size_t chars = 0;
    while (src[srcIdx] != '\0' && chars < maxChars) {
      const uint8_t lead = static_cast<uint8_t>(src[srcIdx]);
      size_t seqLen = 1;
      if ((lead & 0x80u) == 0x00u) {
        seqLen = 1;
      } else if ((lead & 0xE0u) == 0xC0u) {
        seqLen = 2;
      } else if ((lead & 0xF0u) == 0xE0u) {
        seqLen = 3;
      } else if ((lead & 0xF8u) == 0xF0u) {
        seqLen = 4;
      } else {
        // Invalid UTF-8 lead byte: skip 1 byte safely.
        ++srcIdx;
        continue;
      }

      // Verify continuation bytes exist.
      bool valid = true;
      for (size_t k = 1; k < seqLen; ++k) {
        const uint8_t cont = static_cast<uint8_t>(src[srcIdx + k]);
        if (cont == 0 || (cont & 0xC0u) != 0x80u) {
          valid = false;
          break;
        }
      }
      if (!valid) {
        ++srcIdx;
        continue;
      }
      if (srcIdx + seqLen >= dstCap) {
        break;
      }
      for (size_t k = 0; k < seqLen; ++k) {
        dst[srcIdx + k] = src[srcIdx + k];
      }
      srcIdx += seqLen;
      ++chars;
    }
    dst[srcIdx] = '\0';
    return chars;
  }

 private:
  static bool copySanitizedUtf8(const uint8_t* src, size_t len, char* dst,
                                size_t cap) {
    if (!dst || cap == 0) return false;
    char tmp[kMaxFieldBytes] = {};
    size_t outIdx = 0;
    for (size_t i = 0; i < len && outIdx + 1 < sizeof(tmp); ++i) {
      const uint8_t b = src[i];
      if (b == 0) break;
      // Replace control characters (newlines/tabs) with space.
      tmp[outIdx++] = (b < 0x20u) ? ' ' : static_cast<char>(b);
    }
    tmp[outIdx] = '\0';
    char clean[kMaxFieldBytes] = {};
    truncateUtf8CodePoints(tmp, kMaxDisplayChars, clean, sizeof(clean));
    if (std::strcmp(dst, clean) == 0) return false;
    std::snprintf(dst, cap, "%s", clean);
    return true;
  }

  bool ready_ = false;
  bool playing_ = false;
  bool trackChanged_ = false;
  uint8_t supportedCommandsMask_ = 0x7F;
  char title_[kMaxFieldBytes] = {};
  char artist_[kMaxFieldBytes] = {};
};

}  // namespace hid
}  // namespace xuna
