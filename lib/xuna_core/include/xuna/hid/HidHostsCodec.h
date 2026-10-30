#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace xuna {
namespace hid {

// Characteristic 7a6e0015 (HID Hosts V1, 16 bytes, read + notify, UDC-2).
// No Bluetooth addresses leave the board: slots are 0..3 with flags and
// lastSeenDays (0..254, 255 = unknown).
struct HidHostSlotProjection {
  bool used = false;
  bool connected = false;
  bool isAppPhone = false;
  uint8_t lastSeenDays = 0xFF;
};

struct HidHostsProjection {
  bool hidEnabled = true;
  bool hostConnected = false;
  bool remoteActive = false;
  bool pairingWindowOpen = false;
  bool pairingPromptPending = false;
  uint8_t connectedSlot = 0xFF;
  uint8_t bondCount = 0;
  HidHostSlotProjection slots[4] = {};
};

class HidHostsCodec {
 public:
  static constexpr uint8_t kVersion = 1;
  static constexpr size_t kSize = 16;
  static constexpr uint8_t kMaxSlots = 4;
  static constexpr uint8_t kNoSlot = 0xFF;
  static constexpr uint8_t kUnknownDays = 0xFF;

  // Byte 2 flags
  static constexpr uint8_t kFlagHidEnabled = 1u << 0;
  static constexpr uint8_t kFlagHostConnected = 1u << 1;
  static constexpr uint8_t kFlagRemoteActive = 1u << 2;
  static constexpr uint8_t kFlagPairingWindowOpen = 1u << 3;
  static constexpr uint8_t kFlagPairingPromptPending = 1u << 4;

  // Per-slot flags
  static constexpr uint8_t kSlotUsed = 1u << 0;
  static constexpr uint8_t kSlotConnected = 1u << 1;
  static constexpr uint8_t kSlotIsAppPhone = 1u << 2;

  static bool encode(const HidHostsProjection& p, uint8_t* out) {
    if (!out) return false;
    if (p.connectedSlot != kNoSlot && p.connectedSlot >= kMaxSlots) return false;
    std::memset(out, 0, kSize);
    out[0] = kVersion;
    out[1] = uint8_t(kSize);
    uint8_t flags = 0;
    if (p.hidEnabled) flags |= kFlagHidEnabled;
    if (p.hostConnected) flags |= kFlagHostConnected;
    if (p.remoteActive) flags |= kFlagRemoteActive;
    if (p.pairingWindowOpen) flags |= kFlagPairingWindowOpen;
    if (p.pairingPromptPending) flags |= kFlagPairingPromptPending;
    out[2] = flags;
    out[3] = p.connectedSlot;
    out[4] = p.bondCount;
    for (uint8_t i = 0; i < kMaxSlots; ++i) {
      const HidHostSlotProjection& s = p.slots[i];
      uint8_t sf = 0;
      if (s.used) {
        sf |= kSlotUsed;
        if (s.connected) sf |= kSlotConnected;
        if (s.isAppPhone) sf |= kSlotIsAppPhone;
      }
      out[5 + 2 * i] = sf;
      out[6 + 2 * i] = s.used ? s.lastSeenDays : kUnknownDays;
    }
    return true;
  }

  static bool decode(const uint8_t* in, size_t len, HidHostsProjection& out) {
    if (!in || len != kSize) return false;
    if (in[0] != kVersion || in[1] != kSize) return false;
    if ((in[2] & 0xE0u) != 0) return false;
    if (in[3] != kNoSlot && in[3] >= kMaxSlots) return false;
    if (in[4] > kMaxSlots) return false;
    if (in[13] != 0 || in[14] != 0 || in[15] != 0) return false;

    HidHostsProjection parsed;
    parsed.hidEnabled = (in[2] & kFlagHidEnabled) != 0;
    parsed.hostConnected = (in[2] & kFlagHostConnected) != 0;
    parsed.remoteActive = (in[2] & kFlagRemoteActive) != 0;
    parsed.pairingWindowOpen = (in[2] & kFlagPairingWindowOpen) != 0;
    parsed.pairingPromptPending = (in[2] & kFlagPairingPromptPending) != 0;
    parsed.connectedSlot = in[3];
    parsed.bondCount = in[4];
    for (uint8_t i = 0; i < kMaxSlots; ++i) {
      const uint8_t sf = in[5 + 2 * i];
      if ((sf & 0xF8u) != 0) return false;
      HidHostSlotProjection& s = parsed.slots[i];
      s.used = (sf & kSlotUsed) != 0;
      s.connected = s.used && ((sf & kSlotConnected) != 0);
      s.isAppPhone = s.used && ((sf & kSlotIsAppPhone) != 0);
      s.lastSeenDays = s.used ? in[6 + 2 * i] : kUnknownDays;
    }
    out = parsed;
    return true;
  }
};

// Stable 4-slot table keyed by 32-bit FNV-1a hash of the 6-byte peer address.
// Reuses the lowest free slot and refuses a 5th bond until a slot is forgotten.
class HidSlotTable {
 public:
  static constexpr uint8_t kMaxSlots = HidHostsCodec::kMaxSlots;
  static constexpr uint8_t kNoSlot = HidHostsCodec::kNoSlot;
  static constexpr size_t kBlobSize = 36;  // 4-byte header + 4 * 8-byte entries
  static constexpr uint8_t kBlobVersion = 1;

  struct Entry {
    bool used = false;
    bool isAppPhone = false;
    uint32_t addrHash = 0;
    uint16_t lastSeenDay = 0xFFFF;
  };

  static uint32_t hashAddress(const uint8_t bda[6]) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < 6; ++i) {
      h ^= uint32_t(bda[i]);
      h *= 16777619u;
    }
    return h == 0 ? 1u : h;
  }

  int findByHash(uint32_t addrHash) const {
    if (!addrHash) return -1;
    for (uint8_t i = 0; i < kMaxSlots; ++i) {
      if (entries_[i].used && entries_[i].addrHash == addrHash) return int(i);
    }
    return -1;
  }

  int findByAddress(const uint8_t bda[6]) const {
    return findByHash(hashAddress(bda));
  }

  uint8_t usedCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kMaxSlots; ++i) {
      if (entries_[i].used) ++n;
    }
    return n;
  }

  bool canAcceptBond(const uint8_t bda[6]) const {
    if (findByAddress(bda) >= 0) return true;
    return usedCount() < kMaxSlots;
  }

  // Allocates the lowest free slot or updates the existing slot for `addrHash`.
  // Returns 0..3 on success, or -1 when all 4 slots are already occupied.
  int upsertHash(uint32_t addrHash, bool isAppPhone, uint16_t currentDay = 0xFFFF) {
    if (!addrHash) return -1;
    const int existing = findByHash(addrHash);
    if (existing >= 0) {
      Entry& e = entries_[existing];
      if (isAppPhone) e.isAppPhone = true;
      if (currentDay != 0xFFFF) e.lastSeenDay = currentDay;
      return existing;
    }
    for (uint8_t i = 0; i < kMaxSlots; ++i) {
      if (!entries_[i].used) {
        entries_[i].used = true;
        entries_[i].isAppPhone = isAppPhone;
        entries_[i].addrHash = addrHash;
        entries_[i].lastSeenDay = currentDay;
        return int(i);
      }
    }
    return -1;
  }

  int upsertAddress(const uint8_t bda[6], bool isAppPhone, uint16_t currentDay = 0xFFFF) {
    return upsertHash(hashAddress(bda), isAppPhone, currentDay);
  }

  bool markAppPhone(uint32_t addrHash, uint16_t currentDay = 0xFFFF) {
    const int idx = findByHash(addrHash);
    if (idx < 0) return false;
    entries_[idx].isAppPhone = true;
    if (currentDay != 0xFFFF) entries_[idx].lastSeenDay = currentDay;
    return true;
  }

  bool forgetSlot(uint8_t slot) {
    if (slot >= kMaxSlots || !entries_[slot].used) return false;
    entries_[slot] = Entry{};
    return true;
  }

  void forgetAll() {
    for (uint8_t i = 0; i < kMaxSlots; ++i) entries_[i] = Entry{};
  }

  const Entry& entry(uint8_t slot) const { return entries_[slot < kMaxSlots ? slot : 0]; }

  HidHostsProjection buildProjection(bool hidEnabled, uint8_t connectedHostSlot,
                                     uint8_t connectedAppSlot, bool remoteActive,
                                     bool pairingWindowOpen, bool pairingPromptPending,
                                     uint16_t currentDay = 0xFFFF) const {
    HidHostsProjection p;
    p.hidEnabled = hidEnabled;
    p.hostConnected = connectedHostSlot < kMaxSlots && entries_[connectedHostSlot].used;
    p.remoteActive = remoteActive;
    p.pairingWindowOpen = pairingWindowOpen;
    p.pairingPromptPending = pairingPromptPending;
    p.connectedSlot = p.hostConnected ? connectedHostSlot : kNoSlot;
    p.bondCount = usedCount();
    for (uint8_t i = 0; i < kMaxSlots; ++i) {
      const Entry& e = entries_[i];
      HidHostSlotProjection& sp = p.slots[i];
      sp.used = e.used;
      if (!e.used) {
        sp.connected = false;
        sp.isAppPhone = false;
        sp.lastSeenDays = HidHostsCodec::kUnknownDays;
        continue;
      }
      sp.connected = (i == connectedHostSlot) || (i == connectedAppSlot);
      sp.isAppPhone = e.isAppPhone;
      if (sp.connected) {
        sp.lastSeenDays = 0;
      } else if (currentDay != 0xFFFF && e.lastSeenDay != 0xFFFF && currentDay >= e.lastSeenDay) {
        const uint16_t diff = uint16_t(currentDay - e.lastSeenDay);
        sp.lastSeenDays = diff > 254u ? 254u : uint8_t(diff);
      } else {
        sp.lastSeenDays = HidHostsCodec::kUnknownDays;
      }
    }
    return p;
  }

  void encodeBlob(uint8_t out[kBlobSize]) const {
    std::memset(out, 0, kBlobSize);
    out[0] = kBlobVersion;
    out[1] = uint8_t(kBlobSize);
    for (uint8_t i = 0; i < kMaxSlots; ++i) {
      const Entry& e = entries_[i];
      const size_t off = 4 + 8 * i;
      uint8_t flags = 0;
      if (e.used) flags |= 0x01u;
      if (e.isAppPhone) flags |= 0x02u;
      out[off] = flags;
      out[off + 1] = 0;
      out[off + 2] = uint8_t(e.lastSeenDay & 0xFFu);
      out[off + 3] = uint8_t((e.lastSeenDay >> 8) & 0xFFu);
      out[off + 4] = uint8_t(e.addrHash & 0xFFu);
      out[off + 5] = uint8_t((e.addrHash >> 8) & 0xFFu);
      out[off + 6] = uint8_t((e.addrHash >> 16) & 0xFFu);
      out[off + 7] = uint8_t((e.addrHash >> 24) & 0xFFu);
    }
  }

  bool decodeBlob(const uint8_t* in, size_t len) {
    if (!in || len != kBlobSize) return false;
    if (in[0] != kBlobVersion || in[1] != kBlobSize || in[2] != 0 || in[3] != 0) return false;
    Entry parsed[kMaxSlots] = {};
    for (uint8_t i = 0; i < kMaxSlots; ++i) {
      const size_t off = 4 + 8 * i;
      const uint8_t flags = in[off];
      if ((flags & 0xFCu) != 0 || in[off + 1] != 0) return false;
      parsed[i].used = (flags & 0x01u) != 0;
      parsed[i].isAppPhone = parsed[i].used && ((flags & 0x02u) != 0);
      parsed[i].lastSeenDay = uint16_t(in[off + 2]) | (uint16_t(in[off + 3]) << 8);
      parsed[i].addrHash = uint32_t(in[off + 4]) | (uint32_t(in[off + 5]) << 8) |
                           (uint32_t(in[off + 6]) << 16) | (uint32_t(in[off + 7]) << 24);
      if (parsed[i].used && parsed[i].addrHash == 0) return false;
      if (!parsed[i].used) parsed[i] = Entry{};
    }
    for (uint8_t i = 0; i < kMaxSlots; ++i) entries_[i] = parsed[i];
    return true;
  }

 private:
  Entry entries_[kMaxSlots] = {};
};

}  // namespace hid
}  // namespace xuna

