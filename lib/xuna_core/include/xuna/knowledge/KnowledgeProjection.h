#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/knowledge/KnowledgeProfile.h>

namespace xuna {

// Knowledge BLE read-only projection (characteristic 7a6e000f, capability byte 3 bit 4).
// Plan 2026-10-27 §5.3.
// 64 bytes, little-endian:
//    0 u8 version = 1
//    1 u8 size = 64
//    2 u8 flags: bit0 = clock is trusted
//    3 u8 slotCount = 5
//    4 u16 journalSeq
//    6 u16 reserved = 0
//    8 5 slot × 8 B: u8 state, u8 level, u8 facts, u8 days, u16 lastDay, u16 reserved = 0
//   48 4 journal × 4 B, newest first:
//      u16 seq, u8 slot, u8 (level << 4 | reason)

class KnowledgeProjection {
 public:
  static constexpr size_t kSize = 64;
  static constexpr uint8_t kVersion = 1;

  static void encode(const KnowledgeProfile& p, bool clockTrusted, uint8_t out[kSize]) {
    for (size_t i = 0; i < kSize; ++i) out[i] = 0;

    out[0] = kVersion;
    out[1] = static_cast<uint8_t>(kSize);
    out[2] = clockTrusted ? 1 : 0;
    out[3] = KnowledgeProfile::kSlots;
    put16(out + 4, p.journalSeq());
    put16(out + 6, 0);

    for (uint8_t i = 0; i < KnowledgeProfile::kSlots; ++i) {
      const KnowledgeSlot& s = p.slot(i);
      uint8_t* sp = out + 8 + 8 * i;
      sp[0] = static_cast<uint8_t>(s.state);
      sp[1] = s.level;
      sp[2] = s.keyCount;
      sp[3] = s.days;
      put16(sp + 4, s.lastDay);
      put16(sp + 6, 0);
    }

    for (uint8_t age = 0; age < KnowledgeProfile::kJournal; ++age) {
      const KnowledgeJournalEntry& e = p.journal(age);
      uint8_t* jp = out + 48 + 4 * age;
      if (e.seq == 0) continue;
      put16(jp, e.seq);
      jp[2] = e.slot;
      jp[3] = static_cast<uint8_t>((e.level << 4) | (e.reason & 0x0F));
    }
  }

 private:
  static void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
  }
};

}  // namespace xuna
