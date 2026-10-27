#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/knowledge/KnowledgeProfile.h>

namespace xuna {

// Knowledge snapshot section 13 (`SectionId::Knowledge`, optional, version 1).
// Plan 2026-10-27 §5.2.
// Little-endian layout:
//    0 u8 slotCount = 5
//    1 u8 maxKeys = 32
//    2 u16 journalSeq
//    4 per slot (s = 0..4):
//        u8 state, u8 level, u8 keyCount, u8 days, u16 lastDay, u32 keys[keyCount]
//   followed by 4 journal entries (storage order):
//        u16 seq, u8 slot, u8 level, u8 reason, u8 facts, u8 days, u8 reserved = 0
//
// Maximum size: 4 + 5 * (6 + 32 * 4) + 32 = 706 bytes.
// Minimum size: 4 + 5 * 6 + 32 = 66 bytes.

class KnowledgeCodec {
 public:
  static constexpr uint8_t kVersion = 1;
  static constexpr uint8_t kSlotCount = 5;
  static constexpr uint8_t kMaxKeys = 32;
  static constexpr uint8_t kJournalEntries = 4;

  static constexpr size_t kHeaderSize = 4;
  static constexpr size_t kSlotFixedSize = 6;
  static constexpr size_t kJournalSize = kJournalEntries * 8;  // 32 B
  static constexpr size_t kMinSize = kHeaderSize + kSlotCount * kSlotFixedSize + kJournalSize;  // 66 B
  static constexpr size_t kMaxSize =
      kHeaderSize + kSlotCount * (kSlotFixedSize + kMaxKeys * 4) + kJournalSize;  // 706 B

  enum class DecodeResult : uint8_t {
    Ok = 0,
    Corrupt,
    TooNew,
  };

  static size_t encodedSize(const KnowledgeProfile& profile) {
    size_t s = kHeaderSize + kJournalSize;
    for (uint8_t i = 0; i < kSlotCount; ++i) {
      s += kSlotFixedSize + size_t(profile.slot(i).keyCount) * 4;
    }
    return s;
  }

  // Encodes the profile into out. Returns bytes written, or 0 if capacity is insufficient.
  static size_t encode(const KnowledgeProfile& profile, uint8_t* out, size_t capacity) {
    const size_t size = encodedSize(profile);
    if (out == nullptr || capacity < size || size > kMaxSize) return 0;

    out[0] = kSlotCount;
    out[1] = kMaxKeys;
    put16(out + 2, profile.journalSeq());

    uint8_t* p = out + kHeaderSize;
    for (uint8_t i = 0; i < kSlotCount; ++i) {
      const KnowledgeSlot& s = profile.slot(i);
      p[0] = static_cast<uint8_t>(s.state);
      p[1] = s.level;
      p[2] = s.keyCount;
      p[3] = s.days;
      put16(p + 4, s.lastDay);
      p += kSlotFixedSize;
      for (uint8_t k = 0; k < s.keyCount; ++k) {
        put32(p, s.keys[k]);
        p += 4;
      }
    }

    for (uint8_t j = 0; j < kJournalEntries; ++j) {
      const KnowledgeJournalEntry& e = profile.journalSlot(j);
      put16(p, e.seq);
      p[2] = e.slot;
      p[3] = e.level;
      p[4] = e.reason;
      p[5] = e.facts;
      p[6] = e.days;
      p[7] = 0;  // reserved
      p += 8;
    }
    return size;
  }

  // Validates structure and invariants without modifying any profile.
  static bool valid(const uint8_t* in, size_t length) {
    Parsed d;
    return parse(in, length, d) == DecodeResult::Ok;
  }

  // Decodes into profile; returns DecodeResult.
  // Profile is modified ONLY on DecodeResult::Ok.
  static DecodeResult decode(const uint8_t* in, size_t length, KnowledgeProfile& profile) {
    Parsed d;
    const DecodeResult r = parse(in, length, d);
    if (r == DecodeResult::Ok) {
      profile.restore(d.slots, d.journal, d.head, d.journalSeq);
    }
    return r;
  }

  // Adopt stored section; returns true on Ok, false on Corrupt or TooNew.
  // Profile is unmodified on failure.
  static bool load(const uint8_t* in, size_t length, KnowledgeProfile& profile) {
    return decode(in, length, profile) == DecodeResult::Ok;
  }

  static uint16_t get16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8));
  }

  static uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
  }

  static void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
  }

  static void put32(uint8_t* p, uint32_t v) {
    for (uint8_t i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8U * i));
  }

 private:
  struct Parsed {
    uint16_t journalSeq = 0;
    KnowledgeSlot slots[kSlotCount];
    KnowledgeJournalEntry journal[kJournalEntries];
    uint8_t head = 0;
  };

  static DecodeResult parse(const uint8_t* in, size_t length, Parsed& out) {
    if (in == nullptr || length < kMinSize || length > kMaxSize) {
      return DecodeResult::Corrupt;
    }
    const uint8_t slotCount = in[0];
    const uint8_t maxKeys = in[1];
    if (slotCount > kSlotCount || maxKeys > kMaxKeys) {
      return DecodeResult::TooNew;
    }
    if (slotCount != kSlotCount || maxKeys != kMaxKeys) {
      return DecodeResult::Corrupt;
    }
    out.journalSeq = get16(in + 2);

    const uint8_t* p = in + kHeaderSize;
    const uint8_t* journalPtr = in + length - kJournalSize;

    for (uint8_t s = 0; s < kSlotCount; ++s) {
      if (size_t(journalPtr - p) < kSlotFixedSize) return DecodeResult::Corrupt;
      const uint8_t state = p[0];
      const uint8_t level = p[1];
      const uint8_t keyCount = p[2];
      const uint8_t days = p[3];
      const uint16_t lastDay = get16(p + 4);
      p += kSlotFixedSize;

      if (state > 2) return DecodeResult::Corrupt;
      if (keyCount > kMaxKeys) return DecodeResult::Corrupt;
      if (size_t(journalPtr - p) < size_t(keyCount) * 4) return DecodeResult::Corrupt;

      const uint8_t expectedLvl = KnowledgeProfile::computeLevel(keyCount, days);
      if (level != expectedLvl) return DecodeResult::Corrupt;

      out.slots[s].state = static_cast<SlotState>(state);
      out.slots[s].level = level;
      out.slots[s].keyCount = keyCount;
      out.slots[s].days = days;
      out.slots[s].lastDay = lastDay;

      for (uint8_t k = 0; k < keyCount; ++k) {
        const uint32_t key = get32(p + 4 * k);
        if (key == 0) return DecodeResult::Corrupt;
        for (uint8_t prev = 0; prev < k; ++prev) {
          if (out.slots[s].keys[prev] == key) return DecodeResult::Corrupt;
        }
        out.slots[s].keys[k] = key;
      }
      p += keyCount * 4;
    }

    if (p != journalPtr) return DecodeResult::Corrupt;

    for (uint8_t j = 0; j < kJournalEntries; ++j) {
      const uint8_t* jp = journalPtr + 8 * j;
      const uint16_t seq = get16(jp);
      const uint8_t slot = jp[2];
      const uint8_t level = jp[3];
      const uint8_t reason = jp[4];
      const uint8_t facts = jp[5];
      const uint8_t days = jp[6];
      // jp[7] is reserved

      if (seq != 0) {
        if (slot >= kSlotCount || level > KnowledgeProfile::kMaxLevel ||
            reason > 4 || facts > kMaxKeys) {
          return DecodeResult::Corrupt;
        }
      }
      out.journal[j].seq = seq;
      out.journal[j].slot = slot;
      out.journal[j].level = level;
      out.journal[j].reason = reason;
      out.journal[j].facts = facts;
      out.journal[j].days = days;
    }

    if (out.journalSeq == 0) {
      out.head = 0;
    } else {
      int match = -1;
      for (uint8_t j = 0; j < kJournalEntries; ++j) {
        if (out.journal[j].seq == out.journalSeq) {
          match = static_cast<int>(j);
          break;
        }
      }
      if (match >= 0) {
        out.head = static_cast<uint8_t>((match + 1) % kJournalEntries);
      } else {
        return DecodeResult::Corrupt;
      }
    }
    return DecodeResult::Ok;
  }
};

}  // namespace xuna
