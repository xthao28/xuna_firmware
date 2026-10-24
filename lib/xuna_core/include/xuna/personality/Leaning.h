#pragma once

#include <cstdint>

#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/personality/Temperament.h>

namespace xuna {

// Personality PE-4a (plan docs/roadmap/personality, 2026-10-24): the
// temperament starts to change *what* XUNA does, but only where the owner
// has not taught it (plan §5 "trait không đè learning", G1-c). Where nothing
// learned tells actions apart (a tie), a curious pet picks curious more
// often and a playful one happy (an affectionate one when greeted): weight
// 1 ± tieGain, full effect at |trait| 0.3 (the range a pet really has, as
// Expression), never below kMinWeightQ8. Idle stays at 1, the calm
// reference: a pet the owner made less curious and less playful idles more.
// Tempo only sets the pace (Expression): every lab owner's dense sessions
// made it starve idle (PE-4a record). An
// optional margin also lets it decide between actions the owner has barely
// told apart (off on the board: what the owner taught always wins).
// A neutral genome leans nowhere: the choices are those of learning alone,
// bit for bit. Every gain is [P].
//
// Tried in Life Lab and left out (PE-4a record): scaling the UCB bonus by
// curiosity changed nothing visible (a taught cell never explores at ×1.5),
// and drives in the weights made noisy owners' lessons stick less.
struct LeaningConfig {
  uint16_t tieGainQ8 = 256;  // weight 1 ± 1 [P]
  uint16_t marginQ16 = 0;    // near-ties count as ties (0 = exact only) [P]
};

class Leaning {
 public:
  static constexpr int16_t kFullEffect = 4915;  // 0.3 in Q1.14, as Expression
  static constexpr uint16_t kMinWeightQ8 = 32;  // a lean never rules an action out

  // Learner indices 0/1/2 = idle/happy/curious (LearnerVocabulary).
  static V3Leaning forChoice(const TemperamentState& genome, LearnInteraction interaction,
                             const LeaningConfig& config) {
    V3Leaning lean;
    const TraitId warm = interaction == LearnInteraction::ButtonGreeting ? TraitId::Sociability
                                                                         : TraitId::Playfulness;
    lean.weightQ8[1] = weight(Temperament::effective(genome, warm), config);
    lean.weightQ8[2] = weight(Temperament::effective(genome, TraitId::Curiosity), config);
    lean.marginQ16 = config.marginQ16;
    return lean;
  }

 private:
  static uint16_t weight(int32_t trait, const LeaningConfig& config) {
    const int32_t limited = trait > kFullEffect ? kFullEffect : trait < -kFullEffect ? -kFullEffect : trait;
    const int32_t w = 256 + int32_t(config.tieGainQ8) * limited / kFullEffect;
    return static_cast<uint16_t>(w < kMinWeightQ8 ? kMinWeightQ8 : w > 65535 ? 65535 : w);
  }
};

}  // namespace xuna
