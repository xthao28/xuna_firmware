#include <unity.h>

#include <vector>

#include <xuna/BleProtocol.h>
#include <xuna/learning/LearnerV3Explanation.h>
#include <xuna/life/LifeCoreSnapshotLab.h>

using namespace xuna;

namespace {

const uint8_t kHappy = static_cast<uint8_t>(ActionId::Happy);
const uint8_t kCurious = static_cast<uint8_t>(ActionId::Curious);
const uint8_t kSleep = static_cast<uint8_t>(ActionId::Sleep);

std::vector<uint8_t> frame(uint32_t id, uint8_t polarity, uint8_t source,
                           uint8_t contextKey, uint8_t action,
                           uint16_t reserved = 0) {
  std::vector<uint8_t> b(12, 0);
  b[0] = 1;
  b[1] = 6;
  LifeEngine::put32(&b[2], id);
  b[6] = polarity;
  b[7] = source;
  b[8] = contextKey;
  b[9] = action;
  b[10] = static_cast<uint8_t>(reserved);
  b[11] = static_cast<uint8_t>(reserved >> 8);
  return b;
}

V3Decision shown(uint8_t interaction, uint8_t dayPart, uint8_t action,
                 uint32_t id) {
  V3Decision d;
  d.id = id;
  d.interaction = interaction;
  d.dayPart = dayPart;
  d.action = action;
  return d;
}

LifeCoreEvent statement(uint32_t id, uint8_t contextKey, uint8_t action,
                        bool like = true, uint8_t source = 2) {
  return LifeCoreEvent(LifeCoreEventKind::StatedPreference, id, contextKey,
                       action, like, source);
}

LifeCoreSessionStepOutput send(LifeCoreSnapshotLab& lab,
                               const LifeCoreEvent& event, MonoMs at) {
  lab.session().enqueue(event);
  return lab.session().step(at);
}

}  // namespace

// ---- Opcode 6 parse table (plan §11.6) -----------------------------------

void opcode6_parses_only_when_enabled() {
  const std::vector<uint8_t> ok = frame(9, 1, 2, 0x13, kCurious);
  const BleCommand legacy = parseCommand(ok.data(), ok.size());
  TEST_ASSERT_EQUAL_UINT8(3, legacy.error);  // production path unchanged
  const BleCommand c = parseCommand(ok.data(), ok.size(), true);
  TEST_ASSERT_EQUAL_UINT8(0, c.error);
  TEST_ASSERT_EQUAL_UINT8(6, c.opcode);
  TEST_ASSERT_EQUAL_UINT32(9, c.id);
  TEST_ASSERT_EQUAL_UINT8(1, c.arg);
  TEST_ASSERT_EQUAL_UINT8(2, c.source);
  TEST_ASSERT_EQUAL_UINT8(0x13, c.contextKey);
  TEST_ASSERT_EQUAL_UINT8(kCurious, c.action);
}

void opcode6_rejects_malformed_frames() {
  std::vector<uint8_t> shortFrame = frame(9, 1, 2, 0, 1);
  shortFrame.resize(8);
  TEST_ASSERT_EQUAL_UINT8(2, parseCommand(shortFrame.data(), 8, true).error);
  std::vector<uint8_t> bad = frame(9, 2, 2, 0, 1);  // polarity
  TEST_ASSERT_EQUAL_UINT8(4, parseCommand(bad.data(), 12, true).error);
  bad = frame(9, 1, 3, 0, 1);  // voice source waits for D16
  TEST_ASSERT_EQUAL_UINT8(4, parseCommand(bad.data(), 12, true).error);
  bad = frame(9, 1, 1, 0, 1);  // button source is not a statement
  TEST_ASSERT_EQUAL_UINT8(4, parseCommand(bad.data(), 12, true).error);
  bad = frame(9, 1, 2, 0, 1, 7);  // reserved
  TEST_ASSERT_EQUAL_UINT8(4, parseCommand(bad.data(), 12, true).error);
  bad = frame(0, 1, 2, 0, 1);  // id 0
  TEST_ASSERT_EQUAL_UINT8(4, parseCommand(bad.data(), 12, true).error);
  // Existing opcodes keep their behaviour with the flag on.
  const uint8_t choose[8] = {1, 2, 5, 0, 0, 0, 1, 0};
  const BleCommand c = parseCommand(choose, 8, true);
  TEST_ASSERT_EQUAL_UINT8(0, c.error);
  TEST_ASSERT_EQUAL_UINT8(2, c.opcode);
}

// ---- Learner: weight, cap, decay, override --------------------------------

void statement_adds_weighted_stated_evidence_without_a_rating() {
  PreferenceLearnerV3 learner(productLearnerConfig());
  TEST_ASSERT_TRUE(learner.state(0, 3, 1, true));
  const V3CellView coarse = learner.view(0, 0, 1);
  TEST_ASSERT_EQUAL_UINT32(32768, coarse.positiveQ16);  // w_s = 0.5
  TEST_ASSERT_EQUAL_UINT32(32768, coarse.statedPositiveQ16);
  TEST_ASSERT_EQUAL_UINT16(0, coarse.rated);
  // The evening cell got the same evidence (still below m, so the view
  // falls back to coarse).
  TEST_ASSERT_TRUE(learner.state(0, 3, 1, true));
  const V3CellView evening = learner.view(0, 3, 1);
  TEST_ASSERT_TRUE(evening.fine);
  TEST_ASSERT_EQUAL_UINT32(65536, evening.statedPositiveQ16);
  TEST_ASSERT_FALSE(learner.state(3, 0, 1, true));  // unknown interaction
  TEST_ASSERT_FALSE(learner.state(0, 0, 3, true));  // unknown action
}

void cap_holds_under_100_repeated_statements() {
  PreferenceLearnerV3 learner(productLearnerConfig());
  for (int i = 0; i < 100; ++i) learner.state(1, 2, 2, true);
  const V3CellView coarse = learner.view(1, 0, 2);
  TEST_ASSERT_EQUAL_UINT32(2U << 16, coarse.positiveQ16);
  TEST_ASSERT_EQUAL_UINT32(2U << 16, coarse.statedPositiveQ16);
  const V3CellView fine = learner.view(1, 2, 2);
  TEST_ASSERT_EQUAL_UINT32(2U << 16, fine.positiveQ16);
}

void opposite_statement_replaces_the_earlier_one() {
  PreferenceLearnerV3 learner(productLearnerConfig());
  for (int i = 0; i < 4; ++i) learner.state(0, 0, 1, true);
  learner.state(0, 0, 1, false);
  const V3CellView cell = learner.view(0, 0, 1);
  TEST_ASSERT_EQUAL_UINT32(0, cell.positiveQ16);
  TEST_ASSERT_EQUAL_UINT32(0, cell.statedPositiveQ16);
  TEST_ASSERT_EQUAL_UINT32(32768, cell.negativeQ16);
  TEST_ASSERT_EQUAL_UINT32(32768, cell.statedNegativeQ16);
}

void explicit_ratings_override_a_stated_preference() {
  PreferenceLearnerV3 learner(productLearnerConfig());
  for (int i = 0; i < 4; ++i) learner.state(0, 0, 1, true);
  TEST_ASSERT_EQUAL_UINT8(1, learner.greedy(0, 0));
  for (uint32_t id = 1; id <= 6; ++id)
    TEST_ASSERT_TRUE(learner.feedback(shown(0, 0, 1, id), false));
  const V3CellView cell = learner.view(0, 0, 1);
  TEST_ASSERT_TRUE(cell.pQ16 < 32768);
  TEST_ASSERT_TRUE(learner.greedy(0, 0) != 1);
  // Stated mass ages with the ratings and never exceeds the mass.
  TEST_ASSERT_TRUE(cell.statedPositiveQ16 < (2U << 16));
  TEST_ASSERT_TRUE(cell.statedPositiveQ16 <= cell.positiveQ16);
  TEST_ASSERT_EQUAL_UINT16(6, cell.rated);
}

void explanation_reports_stated_mass_separately() {
  PreferenceLearnerV3 learner(productLearnerConfig());
  learner.state(2, 0, 2, true);
  learner.state(2, 0, 2, true);
  V3Explanation e = LearnerV3Explainer::explain(learner, shown(2, 0, 2, 5));
  TEST_ASSERT_TRUE(e.valid);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(V3Reason::Told),
                          static_cast<uint8_t>(e.reason));
  TEST_ASSERT_EQUAL_UINT32(65536, e.cells[2].statedPositiveQ16);
  TEST_ASSERT_EQUAL_UINT16(0, e.cells[2].rated);
  learner.feedback(shown(2, 0, 2, 6), true);
  e = LearnerV3Explainer::explain(learner, shown(2, 0, 2, 7));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(V3Reason::HighestEstimate),
                          static_cast<uint8_t>(e.reason));
  TEST_ASSERT_EQUAL_UINT16(1, e.cells[2].rated);
  TEST_ASSERT_TRUE(e.cells[2].statedPositiveQ16 > 0);
}

// ---- LifeSnapshot v2 section 12 ------------------------------------------

void stated_section_roundtrips_and_is_omitted_when_empty() {
  LifeSnapshotV2 s;
  TEST_ASSERT_EQUAL_UINT32(536, SnapshotCodec::encodedSize(s));
  s.learner.state(0, 3, 1, true);  // coarse + evening cells
  s.learner.state(2, 0, 0, false);
  // 3 cells × 7 B + count + section header.
  TEST_ASSERT_EQUAL_UINT32(536 + 4 + 1 + 3 * 7, SnapshotCodec::encodedSize(s));
  uint8_t bytes[SnapshotCodec::kBudget];
  const size_t size = SnapshotCodec::encode(s, bytes, sizeof(bytes));
  LifeSnapshotV2 out;
  TEST_ASSERT_EQUAL(int(LoadResult::Ok),
                    int(SnapshotCodec::decode(bytes, size, out).result));
  TEST_ASSERT_EQUAL_UINT32(s.learner.fingerprint(), out.learner.fingerprint());
  TEST_ASSERT_EQUAL_UINT32(32768, out.learner.view(2, 0, 0).statedNegativeQ16);
}

void stated_section_validation() {
  PreferenceLearnerV3 learner(productLearnerConfig());
  uint16_t dropped = 0;
  const uint8_t duplicate[] = {2, 0, 0, 1, 128, 0, 0, 0, 0, 0, 1, 128, 0, 0, 0};
  TEST_ASSERT_FALSE(LearnerV3Codec::decodeStated(duplicate, sizeof(duplicate),
                                                 learner, dropped));
  const uint8_t empty[] = {1, 0, 0, 1, 0, 0, 0, 0};
  TEST_ASSERT_FALSE(LearnerV3Codec::decodeStated(empty, sizeof(empty),
                                                 learner, dropped));
  const uint8_t badCount[] = {2, 0, 0, 1, 128, 0, 0, 0};
  TEST_ASSERT_FALSE(LearnerV3Codec::decodeStated(badCount, sizeof(badCount),
                                                 learner, dropped));
  // A share larger than the cell's mass is clamped to the mass.
  learner.state(0, 0, 1, true);  // positive 0.5
  const uint8_t big[] = {1, 0, 0, 1, 0, 2, 0, 0};  // 2.0 stated
  TEST_ASSERT_TRUE(LearnerV3Codec::decodeStated(big, sizeof(big), learner,
                                                dropped));
  TEST_ASSERT_EQUAL_UINT32(32768, learner.view(0, 0, 1).statedPositiveQ16);
}

// ---- LifeCore with the snapshot lab ---------------------------------------

void statement_commits_and_survives_reboot() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  const LifeCoreSessionStepOutput out =
      send(lab, statement(11, 0x13, kCurious), 10);  // play, evening
  TEST_ASSERT_TRUE(out.core.statedApplied);
  TEST_ASSERT_FALSE(out.core.persistRequested);  // committed via the lab
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().snapshot().generation);
  TEST_ASSERT_EQUAL_UINT32(0, lab.session().core().snapshot().counters.feedbackCount);
  const uint32_t learned = lab.session().core().learner().fingerprint();
  TEST_ASSERT_TRUE(lab.reboot(1000));
  TEST_ASSERT_EQUAL_UINT32(learned, lab.session().core().learner().fingerprint());
  TEST_ASSERT_EQUAL_UINT32(
      32768, lab.session().core().learner().view(1, 0, 2).statedPositiveQ16);
}

void invalid_statements_are_rejected() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  const uint32_t before = lab.session().core().learner().fingerprint();
  const LifeCoreEvent bad[] = {
      statement(1, 0x00, kSleep),          // not a learnable action
      statement(2, 0x30, kHappy),          // interaction 3 does not exist
      statement(3, 0x05, kHappy),          // day part 5 does not exist
      statement(4, 0x00, kHappy, true, 3), // voice waits for D16
      statement(5, 0x00, kHappy, true, 1),
      statement(0, 0x00, kHappy),
  };
  for (const LifeCoreEvent& event : bad)
    TEST_ASSERT_TRUE(send(lab, event, 10).core.statedRejected);
  TEST_ASSERT_EQUAL_UINT32(before, lab.session().core().learner().fingerprint());
  TEST_ASSERT_EQUAL_UINT32(0, lab.session().core().snapshot().generation);
}

void duplicate_command_is_applied_once() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_TRUE(send(lab, statement(7, 0x20, kHappy), 10).core.statedApplied);
  const LifeCoreSessionStepOutput again = send(lab, statement(7, 0x20, kHappy), 20);
  TEST_ASSERT_TRUE(again.core.statedDuplicate);
  TEST_ASSERT_FALSE(again.core.statedApplied);
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().snapshot().generation);
  TEST_ASSERT_EQUAL_UINT32(
      32768, lab.session().core().learner().view(2, 0, 1).statedPositiveQ16);
}

void failed_commit_changes_nothing_and_retry_applies() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  const uint32_t before = lab.session().core().learner().fingerprint();
  lab.powerCutNextPersist();
  TEST_ASSERT_TRUE(send(lab, statement(8, 0x00, kCurious), 10).core.statedNotReady);
  TEST_ASSERT_EQUAL_UINT32(before, lab.session().core().learner().fingerprint());
  TEST_ASSERT_TRUE(send(lab, statement(8, 0x00, kCurious), 20).core.statedApplied);
}

void statements_do_not_touch_the_pending_decision() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 3, 1, 0, false, 0, 0};
  TEST_ASSERT_TRUE(send(lab, choose, 10).core.hasDecision);
  const LifeCoreSessionStepOutput out = send(lab, statement(9, 0x10, kCurious), 20);
  TEST_ASSERT_TRUE(out.core.statedApplied);
  TEST_ASSERT_TRUE(out.core.hasDecision);
  TEST_ASSERT_EQUAL_UINT32(3, out.core.decisionId);
}

void error_safe_rejects_statements() {
  LifeCoreSnapshotLab lab(42, 54);
  uint8_t bytes[16] = {'X', 'U', 3, 0, 16, 0, 0, 0, 9, 0, 0, 0};
  const uint32_t crc = crc32(bytes, 12);
  for (int i = 0; i < 4; ++i) bytes[12 + i] = static_cast<uint8_t>(crc >> (8 * i));
  lab.store().put(SnapshotKey::V2Slot1, bytes, 16);
  lab.boot(0);
  TEST_ASSERT_TRUE(send(lab, statement(1, 0x00, kHappy), 10).core.statedRejected);
  TEST_ASSERT_FALSE(lab.store().exists(SnapshotKey::V2Slot0));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(opcode6_parses_only_when_enabled);
  RUN_TEST(opcode6_rejects_malformed_frames);
  RUN_TEST(statement_adds_weighted_stated_evidence_without_a_rating);
  RUN_TEST(cap_holds_under_100_repeated_statements);
  RUN_TEST(opposite_statement_replaces_the_earlier_one);
  RUN_TEST(explicit_ratings_override_a_stated_preference);
  RUN_TEST(explanation_reports_stated_mass_separately);
  RUN_TEST(stated_section_roundtrips_and_is_omitted_when_empty);
  RUN_TEST(stated_section_validation);
  RUN_TEST(statement_commits_and_survives_reboot);
  RUN_TEST(invalid_statements_are_rejected);
  RUN_TEST(duplicate_command_is_applied_once);
  RUN_TEST(failed_commit_changes_nothing_and_retry_applies);
  RUN_TEST(statements_do_not_touch_the_pending_decision);
  RUN_TEST(error_safe_rejects_statements);
  return UNITY_END();
}
