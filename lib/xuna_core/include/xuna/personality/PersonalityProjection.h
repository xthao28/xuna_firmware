#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/personality/Expression.h>

namespace xuna {

// Personality PE-3: read-only BLE projection (characteristic 7a6e000e,
// capability byte 3 bit 1) for the app's personality card and Journal
// (PE-4d). A projection, not a command: nothing here changes XUNA.
// 64 bytes, little-endian:
//    0 u8 version = 1   1 u8 size = 64
//    2 u8 flags: bit0 = the genome is seeded (pet has an identity)
//    3 u8 TraitWord     4 u8 MoodWord     5 u8 reserved = 0
//    6 i16 effective trait[10] (Q1.14, TraitId order)
//   26 i16 mood valence   28 i16 mood arousal (Q1.14)
//   30 u16 owner sessions (saturating)
//   32 journal[4], newest first, empty = all 0:
//      {u16 sequence, u8 trait, u8 reason, u8 source, u8 sessions, i16 delta}
class PersonalityProjection {
 public:
  static constexpr size_t kSize = 64;

  static void encode(const Personality& p, bool seeded, uint8_t out[kSize]) {
    for (size_t i = 0; i < kSize; ++i) out[i] = 0;
    out[0] = 1;
    out[1] = kSize;
    out[2] = seeded ? 1 : 0;
    out[3] = static_cast<uint8_t>(Expression::traitWord(p.genome()));
    const AffectState& a = p.affect().state();
    out[4] = static_cast<uint8_t>(Expression::moodWord(a));
    for (uint8_t t = 0; t < kTraitCount; ++t)
      put16(out + 6 + 2 * t,
            static_cast<uint16_t>(Temperament::effective(p.genome(), static_cast<TraitId>(t))));
    put16(out + 26, static_cast<uint16_t>(a.moodValence));
    put16(out + 28, static_cast<uint16_t>(a.moodArousal));
    put16(out + 30, p.sessions() > 0xffff ? 0xffff : static_cast<uint16_t>(p.sessions()));
    for (uint8_t age = 0; age < EvidenceLedger::kJournalSize; ++age) {
      const JournalEntry& e = p.ledger().journal(age);
      if (e.sequence == 0) continue;
      uint8_t* j = out + 32 + 8 * age;
      put16(j, e.sequence);
      j[2] = e.trait;
      j[3] = e.reason;
      j[4] = e.source;
      j[5] = e.sessions;
      put16(j + 6, static_cast<uint16_t>(e.delta));
    }
  }

 private:
  static void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
  }
};

}  // namespace xuna
