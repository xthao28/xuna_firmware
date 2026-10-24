#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/personality/Personality.h>

namespace xuna {

// Personality PE-3: snapshot section 5 (`SectionId::Personality`, optional,
// version 1). What survives a reboot: learned offsets, the evidence ledger,
// the journal and the counters. The innate baseline is not stored (it comes
// back from IdentityRecord.seed); drives and affect are transient.
//
// Optional (O2, delegated 2026-10-24): an older firmware skips the section
// and keeps running, losing only what XUNA learned about its character; a
// required section would put the whole pet in ERROR_SAFE after a rollback.
//
// Little-endian layout, all offsets in bytes:
//    0 u32 session   4 u32 evidence   8 u32 shifts   12 u32 decays
//   16 i16 offset[10]
//   36 u8 journal head   37 u16 journal sequence
//   39 journal[4] {u16 sequence, u8 trait, u8 reason, u8 source, u8 sessions, i16 delta}
//   71 u8 trait count, then per trait with evidence, ascending id:
//      u8 id, i32 pending, i32 importance, u32 lastSession, i32 sessionNet,
//      i32 sessionMagnitude, u8 sessions, u8 positive, u8 negative,
//      u8 n ≤ 3, n × {u8 reason, u16 weight}, u8 m ≤ 2, m × {u8 source, u16 weight}
// At most the three heaviest reasons and two heaviest sources per trait are
// kept (ties: lower id). The dominant one — all the journal uses — always
// survives; today each trait has at most two reasons and one source, so
// nothing is dropped. This bounds the section to kMaxSize whatever happens.
// Nothing is written while XUNA has learned nothing (no offset, no evidence,
// no journal), so snapshots without personality stay byte-identical.
class PersonalityCodec {
 public:
  static constexpr uint8_t kVersion = 1;
  static constexpr uint8_t kMaxReasons = 3;
  static constexpr uint8_t kMaxSources = 2;
  static constexpr size_t kFixedSize = 72;
  static constexpr size_t kJournalAt = 39;
  static constexpr size_t kTraitFixedSize = 24;  // id … negative
  static constexpr size_t kMaxTraitSize =
      kTraitFixedSize + 1 + 3 * kMaxReasons + 1 + 3 * kMaxSources;
  static constexpr size_t kMaxSize = kFixedSize + kTraitCount * kMaxTraitSize;  // 482

  // Bytes written (0 = nothing learned yet, or `capacity` too small).
  static size_t encode(const Personality& p, uint8_t* out, size_t capacity) {
    const EvidenceLedger& l = p.ledger();
    bool any = l.journalSequence() != 0;
    for (uint8_t t = 0; t < kTraitCount; ++t)
      any = any || p.genome().offset[t] != 0 || !empty(l.trait(static_cast<TraitId>(t)));
    if (!any || out == 0 || capacity < kMaxSize) return 0;
    put32(out, p.sessions());
    put32(out + 4, p.evidence());
    put32(out + 8, p.shifts());
    put32(out + 12, p.decays());
    for (uint8_t t = 0; t < kTraitCount; ++t)
      put16(out + 16 + 2 * t, static_cast<uint16_t>(p.genome().offset[t]));
    out[36] = l.journalHead();
    put16(out + 37, l.journalSequence());
    for (uint8_t i = 0; i < EvidenceLedger::kJournalSize; ++i) {
      const JournalEntry& e = l.journalSlot(i);
      uint8_t* j = out + kJournalAt + 8 * i;
      put16(j, e.sequence);
      j[2] = e.trait;
      j[3] = e.reason;
      j[4] = e.source;
      j[5] = e.sessions;
      put16(j + 6, static_cast<uint16_t>(e.delta));
    }
    uint8_t* p8 = out + kFixedSize;
    uint8_t count = 0;
    for (uint8_t t = 0; t < kTraitCount; ++t) {
      const TraitLedger& tl = l.trait(static_cast<TraitId>(t));
      if (empty(tl)) continue;
      ++count;
      p8[0] = t;
      put32(p8 + 1, static_cast<uint32_t>(tl.pending));
      put32(p8 + 5, static_cast<uint32_t>(tl.importance));
      put32(p8 + 9, tl.lastSession);
      put32(p8 + 13, static_cast<uint32_t>(tl.sessionNet));
      put32(p8 + 17, static_cast<uint32_t>(tl.sessionMagnitude));
      p8[21] = tl.sessions;
      p8[22] = tl.positive;
      p8[23] = tl.negative;
      p8 = weights(p8 + kTraitFixedSize, tl.reasonWeight, kEvidenceReasonCount, kMaxReasons);
      p8 = weights(p8, tl.sourceWeight, kEvidenceSourceCount, kMaxSources);
    }
    out[kFixedSize - 1] = count;
    return size_t(p8 - out);
  }

  // Structure and ranges only (SnapshotCodec decode); nothing is changed.
  // Every check is an invariant the encoder keeps (saturating counters and
  // the wrapping journal sequence are not checked against each other).
  static bool valid(const uint8_t* in, size_t length) {
    Parsed parsed;
    return parse(in, length, parsed);
  }

  // Adopt a stored section on top of the seeded baseline (boot). False and
  // `p` unchanged when the bytes are invalid.
  static bool load(const uint8_t* in, size_t length, Personality& p) {
    Parsed d;
    if (!parse(in, length, d)) return false;
    p.restore(d.offset, d.session, d.evidence, d.shifts, d.decays, d.traits, d.journal, d.head,
              d.sequence);
    return true;
  }

 private:
  struct Parsed {
    uint32_t session = 0, evidence = 0, shifts = 0, decays = 0;
    int16_t offset[kTraitCount] = {};
    uint8_t head = 0;
    uint16_t sequence = 0;
    JournalEntry journal[EvidenceLedger::kJournalSize];
    TraitLedger traits[kTraitCount];
  };

  static bool empty(const TraitLedger& t) {
    if (t.pending || t.importance || t.lastSession || t.sessionNet || t.sessionMagnitude ||
        t.sessions || t.positive || t.negative)
      return false;
    for (uint8_t i = 0; i < kEvidenceReasonCount; ++i)
      if (t.reasonWeight[i]) return false;
    for (uint8_t i = 0; i < kEvidenceSourceCount; ++i)
      if (t.sourceWeight[i]) return false;
    return true;
  }

  // The `keep` heaviest non-zero weights (ties: lower id), written in id order.
  static uint8_t* weights(uint8_t* p, const uint16_t* w, uint8_t count, uint8_t keep) {
    bool chosen[kEvidenceReasonCount] = {};
    uint8_t n = 0;
    for (uint8_t k = 0; k < keep; ++k) {
      int best = -1;
      for (uint8_t i = 0; i < count; ++i)
        if (w[i] && !chosen[i] && (best < 0 || w[i] > w[best])) best = i;
      if (best < 0) break;
      chosen[best] = true;
      ++n;
    }
    p[0] = n;
    uint8_t* q = p + 1;
    for (uint8_t i = 0; i < count; ++i) {
      if (!chosen[i]) continue;
      q[0] = i;
      put16(q + 1, w[i]);
      q += 3;
    }
    return q;
  }

  static bool readWeights(const uint8_t*& p, const uint8_t* end, uint16_t* w, uint8_t count,
                          uint8_t keep) {
    if (p >= end) return false;
    const uint8_t n = p[0];
    ++p;
    if (n > keep || size_t(end - p) < 3U * n) return false;
    int last = -1;
    for (uint8_t k = 0; k < n; ++k, p += 3) {
      const uint8_t id = p[0];
      const uint16_t value = get16(p + 1);
      if (id >= count || int(id) <= last || value == 0) return false;
      w[id] = value;
      last = id;
    }
    return true;
  }

  static bool parse(const uint8_t* in, size_t length, Parsed& d) {
    if (in == 0 || length < kFixedSize || length > kMaxSize) return false;
    d.session = get32(in);
    d.evidence = get32(in + 4);
    d.shifts = get32(in + 8);
    d.decays = get32(in + 12);
    for (uint8_t t = 0; t < kTraitCount; ++t) {
      d.offset[t] = static_cast<int16_t>(get16(in + 16 + 2 * t));
      if (d.offset[t] > kOffsetRange || d.offset[t] < -kOffsetRange) return false;
    }
    d.head = in[36];
    d.sequence = get16(in + 37);
    if (d.head >= EvidenceLedger::kJournalSize) return false;
    for (uint8_t i = 0; i < EvidenceLedger::kJournalSize; ++i) {
      const uint8_t* j = in + kJournalAt + 8 * i;
      JournalEntry& e = d.journal[i];
      e.sequence = get16(j);
      e.trait = j[2];
      e.reason = j[3];
      e.source = j[4];
      e.sessions = j[5];
      e.delta = static_cast<int16_t>(get16(j + 6));
      // No ordering check against d.sequence: the counter wraps at 65535.
      if (e.trait >= kTraitCount ||
          e.reason >= kEvidenceReasonCount || e.source >= kEvidenceSourceCount ||
          e.delta > kOffsetRange || e.delta < -kOffsetRange)
        return false;
    }
    const uint8_t count = in[kFixedSize - 1];
    if (count > kTraitCount) return false;
    const uint8_t* p = in + kFixedSize;
    const uint8_t* end = in + length;
    int last = -1;
    for (uint8_t k = 0; k < count; ++k) {
      if (size_t(end - p) < kTraitFixedSize) return false;
      const uint8_t id = p[0];
      if (id >= kTraitCount || int(id) <= last) return false;
      last = id;
      TraitLedger& t = d.traits[id];
      t.pending = static_cast<int32_t>(get32(p + 1));
      t.importance = static_cast<int32_t>(get32(p + 5));
      t.lastSession = get32(p + 9);
      t.sessionNet = static_cast<int32_t>(get32(p + 13));
      t.sessionMagnitude = static_cast<int32_t>(get32(p + 17));
      t.sessions = p[21];
      t.positive = p[22];
      t.negative = p[23];
      if (t.pending > EvidenceLedger::kMaxPending || t.pending < -EvidenceLedger::kMaxPending ||
          t.importance < 0 || t.sessionMagnitude < 0 || t.lastSession > d.session)
        return false;
      p += kTraitFixedSize;
      if (!readWeights(p, end, t.reasonWeight, kEvidenceReasonCount, kMaxReasons) ||
          !readWeights(p, end, t.sourceWeight, kEvidenceSourceCount, kMaxSources))
        return false;
    }
    return p == end;
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
};

}  // namespace xuna
