#include <unity.h>

#include <xuna/life/LifeCore.h>
#include <xuna/personality/PersonalityCodec.h>

using namespace xuna;

// PE-4c (2026-10-25): closed conversation tags change XUNA's feelings at once
// and are weak, capped evidence for one trait; learning is untouched (O4).

namespace {

const uint32_t kSeed = 0x5eed1234U;
const uint32_t kMinute = 60000;

struct Expect {
  ConversationTag tag;
  TraitId trait;
  int sign;
  EvidenceReason reason;
  int valence;  // sign of the emotion it causes
};

const Expect kTags[] = {
    {ConversationTag::Praise, TraitId::Sociability, 1, EvidenceReason::ConversationPraise, 1},
    {ConversationTag::Scold, TraitId::Sociability, -1, EvidenceReason::ConversationScold, -1},
    {ConversationTag::Affection, TraitId::Sociability, 1, EvidenceReason::ConversationAffection, 1},
    {ConversationTag::Playful, TraitId::Playfulness, 1, EvidenceReason::ConversationPlay, 1},
    {ConversationTag::CuriousQuestion, TraitId::Curiosity, 1, EvidenceReason::ConversationQuestion,
     1},
    {ConversationTag::LongChat, TraitId::Sociability, 1, EvidenceReason::ConversationLong, 1},
};

// One owner session at `start` with `count` tags, closed after the gap.
void talk(Personality& p, uint32_t start, ConversationTag tag, uint32_t count) {
  p.ownerInput(start);
  for (uint32_t i = 0; i < count; ++i) p.conversation(tag, start + i * 1000);
  p.step(start + 31 * kMinute, Activity::Idle, 40000);
}

}  // namespace

void every_tag_is_felt_and_noted_for_its_trait() {
  for (const Expect& e : kTags) {
    Personality p(kSeed);
    p.begin(0);
    p.ownerInput(1000);  // the owner's arrival is a welcome of its own
    const int16_t welcomed = p.affect().state().emotionValence;
    TEST_ASSERT_TRUE(p.conversation(e.tag, 1000));
    const int valence = p.affect().state().emotionValence - welcomed;
    TEST_ASSERT_TRUE(e.valence > 0 ? valence > 0 : valence < 0);
    TEST_ASSERT_EQUAL_UINT32(1, p.evidence());
    const TraitLedger& l = p.ledger().trait(e.trait);
    // Weight 0.1 (ConversationInferred), never more than device observed.
    TEST_ASSERT_EQUAL_INT32(e.sign * 1638, l.sessionNet);
    TEST_ASSERT_TRUE(l.reasonWeight[static_cast<uint8_t>(e.reason)] > 0);
    TEST_ASSERT_TRUE(l.sourceWeight[static_cast<uint8_t>(EvidenceSource::ConversationInferred)] > 0);
  }
  TEST_ASSERT_TRUE(EvidenceLedger::weight(EvidenceSource::ConversationInferred) <=
                   EvidenceLedger::weight(EvidenceSource::DeviceObserved));
}

void unknown_tags_and_closed_sessions_are_ignored() {
  Personality p(kSeed);
  p.begin(0);
  TEST_ASSERT_FALSE(p.conversation(ConversationTag::Praise, 1000));  // no owner session
  p.ownerInput(2000);
  const int16_t welcomed = p.affect().state().emotionValence;
  TEST_ASSERT_FALSE(p.conversation(ConversationTag::None, 2000));
  TEST_ASSERT_FALSE(p.conversation(static_cast<ConversationTag>(kConversationTagCount), 2000));
  TEST_ASSERT_EQUAL_UINT32(0, p.evidence());
  TEST_ASSERT_EQUAL_INT16(welcomed, p.affect().state().emotionValence);
}

void tags_are_capped_per_day() {
  Personality p(kSeed);
  p.begin(0);
  p.ownerInput(1000);
  for (uint8_t i = 0; i < Personality::kConversationDailyCap; ++i)
    TEST_ASSERT_TRUE(p.conversation(ConversationTag::Playful, 1000 + i));
  TEST_ASSERT_FALSE(p.conversation(ConversationTag::Playful, 2000));
  TEST_ASSERT_FALSE(p.conversation(ConversationTag::Praise, 3000));
  // A day later the owner is back and XUNA listens again.
  const uint32_t later = Personality::kDayMs + 1000;
  p.ownerInput(later);
  TEST_ASSERT_TRUE(p.conversation(ConversationTag::Praise, later));
}

void talk_alone_needs_many_sessions_to_move_a_trait() {
  Personality p(kSeed);
  p.begin(0);
  const int16_t before = p.genome().offset[static_cast<uint8_t>(TraitId::Sociability)];
  // Three days of heavy praise: the session share and the ≥ 4 sessions rule hold it.
  for (uint32_t d = 0; d < 3; ++d) talk(p, d * Personality::kDayMs + 1000, ConversationTag::Praise, 8);
  TEST_ASSERT_EQUAL_INT16(before, p.genome().offset[static_cast<uint8_t>(TraitId::Sociability)]);
  TEST_ASSERT_EQUAL_UINT32(0, p.shifts());
  // A fourth consistent day moves it by at most one small step.
  talk(p, 3 * Personality::kDayMs + 1000, ConversationTag::Praise, 8);
  const int16_t after = p.genome().offset[static_cast<uint8_t>(TraitId::Sociability)];
  TEST_ASSERT_TRUE(after > before);
  TEST_ASSERT_TRUE(after - before <= EvidenceLedger::kMaxStep);
  const JournalEntry& entry = p.ledger().journal(0);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EvidenceReason::ConversationPraise), entry.reason);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EvidenceSource::ConversationInferred), entry.source);
}

void mixed_praise_and_scold_moves_nothing() {
  Personality p(kSeed);
  p.begin(0);
  for (uint32_t d = 0; d < 8; ++d)
    talk(p, d * Personality::kDayMs + 1000,
         d % 2 ? ConversationTag::Scold : ConversationTag::Praise, 5);
  TEST_ASSERT_EQUAL_INT16(0, p.genome().offset[static_cast<uint8_t>(TraitId::Sociability)]);
}

void conversation_evidence_survives_a_reboot() {
  Personality p(kSeed);
  p.begin(0);
  p.ownerInput(1000);
  p.conversation(ConversationTag::CuriousQuestion, 1000);
  p.conversation(ConversationTag::LongChat, 2000);
  uint8_t out[PersonalityCodec::kMaxSize];
  const size_t n = PersonalityCodec::encode(p, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  Personality q(kSeed);
  q.begin(0);
  TEST_ASSERT_TRUE(PersonalityCodec::load(out, n, q));
  const TraitLedger& c = q.ledger().trait(TraitId::Curiosity);
  TEST_ASSERT_TRUE(c.reasonWeight[static_cast<uint8_t>(EvidenceReason::ConversationQuestion)] > 0);
  const TraitLedger& s = q.ledger().trait(TraitId::Sociability);
  TEST_ASSERT_TRUE(s.reasonWeight[static_cast<uint8_t>(EvidenceReason::ConversationLong)] > 0);
}

void lifecore_conversation_touches_feelings_not_learning() {
  LifeCore core;
  core.seedPersonality(kSeed);
  core.begin(0, true);
  core.step(LifeCoreStepInput(1000));
  const uint32_t feedback = core.snapshot().counters.feedbackCount;
  const LifeCoreEvent praise(LifeCoreEventKind::Conversation, 7, 0,
                             static_cast<uint8_t>(ConversationTag::Praise));
  const LifeCoreStepOutput out = core.step(LifeCoreStepInput(2000, &praise, 1));
  TEST_ASSERT_TRUE(out.conversationApplied);
  TEST_ASSERT_FALSE(out.decisionShown);
  TEST_ASSERT_FALSE(out.learningFeedbackApplied);
  TEST_ASSERT_TRUE(core.personality().affect().state().emotionValence > 0);
  TEST_ASSERT_EQUAL_UINT32(1, core.personality().evidence());
  TEST_ASSERT_EQUAL_UINT32(feedback, core.snapshot().counters.feedbackCount);
  const LifeCoreEvent bogus(LifeCoreEventKind::Conversation, 8, 0, 9);
  TEST_ASSERT_FALSE(core.step(LifeCoreStepInput(3000, &bogus, 1)).conversationApplied);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(every_tag_is_felt_and_noted_for_its_trait);
  RUN_TEST(unknown_tags_and_closed_sessions_are_ignored);
  RUN_TEST(tags_are_capped_per_day);
  RUN_TEST(talk_alone_needs_many_sessions_to_move_a_trait);
  RUN_TEST(mixed_praise_and_scold_moves_nothing);
  RUN_TEST(conversation_evidence_survives_a_reboot);
  RUN_TEST(lifecore_conversation_touches_feelings_not_learning);
  return UNITY_END();
}
