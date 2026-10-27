#pragma once

#include <cstddef>
#include <cstdint>

namespace xuna {

// Knowledge domains (M06 KD-1, plan 2026-10-27 §5.1).
// The physical device is the authority on slots, counters, fact keys,
// level, and journal. Domain names and descriptions remain in the cloud/app.

enum class SlotState : uint8_t {
  Empty = 0,
  Active = 1,
  Resting = 2
};

enum class KnowledgeReason : uint8_t {
  None = 0,
  EvidenceAdded = 1,
  EvidenceRemoved = 2,
  Cleared = 3,
  Moved = 4
};

struct KnowledgeSlot {
  SlotState state = SlotState::Empty;
  uint8_t level = 0;          // 0..3, derived, never set from outside
  uint8_t keyCount = 0;       // 0..kMaxKeys; == counted facts
  uint8_t days = 0;           // distinct teaching days, saturating at 255
  uint16_t lastDay = 0;       // local day index since 2020-01-01; 0 = none
  uint32_t keys[32] = {};     // fact keys, insertion order
};

struct KnowledgeJournalEntry {
  uint16_t seq = 0;
  uint8_t slot = 0;
  uint8_t level = 0;
  uint8_t reason = 0;
  uint8_t facts = 0;
  uint8_t days = 0;
};

class KnowledgeProfile {
 public:
  static constexpr uint8_t kSlots = 5;
  static constexpr uint8_t kMaxKeys = 32;
  static constexpr uint8_t kJournal = 4;
  static constexpr uint8_t kMaxLevel = 3;

  enum class Result : uint8_t {
    Ok = 0,
    Duplicate,
    NotFound,
    SlotBusy,
    SlotEmpty,
    SlotResting,
    Full,
    Invalid
  };

  // Level thresholds: F = {3, 10, 25}, D = {1, 3, 5}.
  // Level = number of indices i in {0, 1, 2} such that facts >= F[i] and days >= D[i].
  static uint8_t computeLevel(uint8_t facts, uint8_t days) {
    static const uint8_t kF[3] = {3, 10, 25};
    static const uint8_t kD[3] = {1, 3, 5};
    uint8_t lvl = 0;
    for (uint8_t i = 0; i < 3; ++i) {
      if (facts >= kF[i] && days >= kD[i]) {
        ++lvl;
      }
    }
    return lvl;
  }

  // Open an Empty slot -> Active.
  Result open(uint8_t slot) {
    if (slot >= kSlots) return Result::Invalid;
    if (slots_[slot].state != SlotState::Empty) return Result::SlotBusy;
    slots_[slot] = KnowledgeSlot();
    slots_[slot].state = SlotState::Active;
    return Result::Ok;
  }

  // Active <-> Resting. Empty cannot be set via setState (use clear).
  Result setState(uint8_t slot, SlotState s) {
    if (slot >= kSlots || s == SlotState::Empty) return Result::Invalid;
    if (slots_[slot].state == SlotState::Empty) return Result::SlotEmpty;
    slots_[slot].state = s;
    return Result::Ok;
  }

  // Any state -> Empty, drops keys, journals reason Cleared.
  Result clear(uint8_t slot) {
    if (slot >= kSlots) return Result::Invalid;
    if (slots_[slot].state == SlotState::Empty && slots_[slot].keyCount == 0 && slots_[slot].days == 0) {
      return Result::Ok;
    }
    slots_[slot] = KnowledgeSlot();
    recordJournal(slot, 0, static_cast<uint8_t>(KnowledgeReason::Cleared), 0, 0);
    return Result::Ok;
  }

  // Add evidence to an Active slot.
  // Idempotent: adding an already existing key returns Duplicate without changing state or days.
  // trustedDay == 0 counts facts but does not count days.
  Result add(uint8_t slot, uint32_t key, uint16_t trustedDay /* 0 = untrusted */) {
    if (slot >= kSlots || key == 0) return Result::Invalid;
    if (slots_[slot].state == SlotState::Empty) return Result::SlotEmpty;
    if (slots_[slot].state == SlotState::Resting) return Result::SlotResting;

    for (uint8_t i = 0; i < slots_[slot].keyCount; ++i) {
      if (slots_[slot].keys[i] == key) return Result::Duplicate;
    }
    if (slots_[slot].keyCount >= kMaxKeys) return Result::Full;

    slots_[slot].keys[slots_[slot].keyCount++] = key;
    if (trustedDay != 0 && trustedDay != slots_[slot].lastDay) {
      slots_[slot].lastDay = trustedDay;
      if (slots_[slot].days < 255) {
        ++slots_[slot].days;
      }
    }
    slots_[slot].level = computeLevel(slots_[slot].keyCount, slots_[slot].days);
    recordJournal(slot, slots_[slot].level,
                  static_cast<uint8_t>(KnowledgeReason::EvidenceAdded),
                  slots_[slot].keyCount, slots_[slot].days);
    return Result::Ok;
  }

  // Remove evidence from an Active or Resting slot.
  // Days counter is not reduced (historical fact).
  Result remove(uint8_t slot, uint32_t key) {
    if (slot >= kSlots || key == 0) return Result::Invalid;
    if (slots_[slot].state == SlotState::Empty) return Result::SlotEmpty;

    int found = -1;
    for (uint8_t i = 0; i < slots_[slot].keyCount; ++i) {
      if (slots_[slot].keys[i] == key) {
        found = static_cast<int>(i);
        break;
      }
    }
    if (found < 0) return Result::NotFound;

    for (uint8_t i = static_cast<uint8_t>(found); i + 1 < slots_[slot].keyCount; ++i) {
      slots_[slot].keys[i] = slots_[slot].keys[i + 1];
    }
    slots_[slot].keys[slots_[slot].keyCount - 1] = 0;
    --slots_[slot].keyCount;

    slots_[slot].level = computeLevel(slots_[slot].keyCount, slots_[slot].days);
    recordJournal(slot, slots_[slot].level,
                  static_cast<uint8_t>(KnowledgeReason::EvidenceRemoved),
                  slots_[slot].keyCount, slots_[slot].days);
    return Result::Ok;
  }

  // Move a fact key from one slot to another.
  Result move(uint8_t from, uint8_t to, uint32_t key, uint16_t trustedDay) {
    if (from >= kSlots || to >= kSlots || from == to || key == 0) {
      return Result::Invalid;
    }
    if (slots_[from].state == SlotState::Empty) return Result::SlotEmpty;
    if (slots_[to].state == SlotState::Empty) return Result::SlotEmpty;
    if (slots_[to].state == SlotState::Resting) return Result::SlotResting;
    if (slots_[to].keyCount >= kMaxKeys) return Result::Full;

    int found = -1;
    for (uint8_t i = 0; i < slots_[from].keyCount; ++i) {
      if (slots_[from].keys[i] == key) {
        found = static_cast<int>(i);
        break;
      }
    }
    if (found < 0) return Result::NotFound;

    for (uint8_t i = 0; i < slots_[to].keyCount; ++i) {
      if (slots_[to].keys[i] == key) return Result::Duplicate;
    }

    // Remove from 'from'
    for (uint8_t i = static_cast<uint8_t>(found); i + 1 < slots_[from].keyCount; ++i) {
      slots_[from].keys[i] = slots_[from].keys[i + 1];
    }
    slots_[from].keys[slots_[from].keyCount - 1] = 0;
    --slots_[from].keyCount;
    slots_[from].level = computeLevel(slots_[from].keyCount, slots_[from].days);

    // Add to 'to'
    slots_[to].keys[slots_[to].keyCount++] = key;
    if (trustedDay != 0 && trustedDay != slots_[to].lastDay) {
      slots_[to].lastDay = trustedDay;
      if (slots_[to].days < 255) {
        ++slots_[to].days;
      }
    }
    slots_[to].level = computeLevel(slots_[to].keyCount, slots_[to].days);

    // Journal for both slots
    recordJournal(from, slots_[from].level,
                  static_cast<uint8_t>(KnowledgeReason::Moved),
                  slots_[from].keyCount, slots_[from].days);
    recordJournal(to, slots_[to].level,
                  static_cast<uint8_t>(KnowledgeReason::Moved),
                  slots_[to].keyCount, slots_[to].days);
    return Result::Ok;
  }

  const KnowledgeSlot& slot(uint8_t i) const {
    return slots_[i < kSlots ? i : 0];
  }

  // Newest journal entry first: age 0 = latest.
  const KnowledgeJournalEntry& journal(uint8_t age) const {
    return journal_[(head_ + kJournal - 1 - (age % kJournal)) % kJournal];
  }

  const KnowledgeJournalEntry& journalSlot(uint8_t i) const {
    return journal_[i % kJournal];
  }

  uint8_t journalHead() const { return head_; }
  uint16_t journalSeq() const { return sequence_; }

  bool empty() const {
    if (sequence_ != 0) return false;
    for (uint8_t i = 0; i < kSlots; ++i) {
      if (slots_[i].state != SlotState::Empty || slots_[i].keyCount != 0 || slots_[i].days != 0) {
        return false;
      }
    }
    return true;
  }

  void clearAll() {
    for (uint8_t i = 0; i < kSlots; ++i) slots_[i] = KnowledgeSlot();
    for (uint8_t i = 0; i < kJournal; ++i) journal_[i] = KnowledgeJournalEntry();
    head_ = 0;
    sequence_ = 0;
  }

  void restore(const KnowledgeSlot* slots, const KnowledgeJournalEntry* journal,
               uint8_t head, uint16_t sequence) {
    for (uint8_t i = 0; i < kSlots; ++i) slots_[i] = slots[i];
    for (uint8_t i = 0; i < kJournal; ++i) journal_[i] = journal[i];
    head_ = static_cast<uint8_t>(head % kJournal);
    sequence_ = sequence;
  }

 private:
  void recordJournal(uint8_t slot, uint8_t level, uint8_t reason, uint8_t facts, uint8_t days) {
    KnowledgeJournalEntry& e = journal_[head_];
    e.seq = ++sequence_ ? sequence_ : (sequence_ = 1);
    e.slot = slot;
    e.level = level;
    e.reason = reason;
    e.facts = facts;
    e.days = days;
    head_ = static_cast<uint8_t>((head_ + 1) % kJournal);
  }

  KnowledgeSlot slots_[kSlots];
  KnowledgeJournalEntry journal_[kJournal];
  uint8_t head_ = 0;
  uint16_t sequence_ = 0;
};

}  // namespace xuna
