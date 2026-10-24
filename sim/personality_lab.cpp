// Personality PE-0 baseline and PE-1d shadow (plan docs/roadmap/personality,
// 2026-10-23). How far do XUNAs differ through learning alone, and what would
// the temperament genome, drives and affect add (PersonalityShadow)?
// Host only, lab only; nothing here runs on the board:
//   c++ -std=c++11 -O2 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/personality_lab.cpp -o personality_lab
//   ./personality_lab > pe0.txt      (deterministic; runtime goes to stderr)
//   ./personality_lab leaning        (PE-4a lean sweep alone)
// Since PE-4a the board's lives lean their untaught choices by temperament
// (Leaning.h, board defaults); the sweep compares the gains.
//
// Every run boots BoardLab (the board's setup()/loop() replica) from a fresh
// migrated v1 pet and lives `days` local days in 1 s steps. A simulated owner
// (OwnerModel) is around twice a day (07:00 for 30 min, 19:00 for 2 h, each
// ±30 min), syncs time from the app when it arrives, presses GPIO0 every
// 15 min to greet and reacts to part of XUNA's choices while present: a pet
// (GPIO0) when it approves, a shoo (GPIO14) when it does not. Outside those
// windows XUNA keeps acting on its own and nobody reacts.
//
// Behaviour = what XUNA shows: counts of idle/happy/curious per learner cell
// (interaction quiet/greeting × day part). Distance between two behaviours =
// mean Jensen–Shannon divergence (bits, 0…1) over cells both sampled enough.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <xuna/lab/BoardLab.h>
#include <xuna/lab/OwnerModel.h>
#include <xuna/lab/PersonalityShadow.h>
#include <xuna/learning/LearnerVocabulary.h>

#include "../test/fixtures/v1_snapshots.h"

namespace {

using namespace xuna;

constexpr uint32_t kMaxDays = 90;      // capacity; millis() wraps after 49.7 days (LifeCore extends it)
constexpr uint32_t kDefaultDays = 30;
constexpr uint8_t kInteractions = kLearnInteractionCount;
constexpr uint8_t kParts = LearnerV3Config::kMaxDayParts;
constexpr uint8_t kActions = kLearnActionCount;
constexpr uint64_t kMinuteMs = 60000ULL;
constexpr uint32_t kUtcBase = 1792026000U;  // 2026-10-15 05:00 UTC = 12:00 local
constexpr int16_t kTz = 420;                // Asia/Ho_Chi_Minh
constexpr uint32_t kNever = 0xFFFFFFFFU;
// Last week of a 30-day life: local days 23–29.
constexpr uint32_t kWeekFrom = 23, kWeekTo = 29;
// Cells compared: quiet (spontaneous) and greeting; play needs the app.
const uint8_t kCompared[] = {0, 2};

struct Day {
  uint32_t shown[kInteractions][kParts][kActions];
  uint8_t greedy[kInteractions][kParts];  // learner at the end of the day
  uint32_t fingerprint;
  uint32_t decisions, pets, shoos;
  // PE-1d shadow
  uint32_t shadow[kInteractions][kParts][kActions];  // what drives + genome would show
  int16_t trait[kTraitCount];                         // effective, end of day
  int64_t moodSum;                                    // mood valence, one sample a minute
  int16_t moodMin, moodMax;
  uint32_t moodSamples;
  uint32_t restMinutes;  // PE-2b: minutes Resting or Sleeping
};

struct Life {
  Day day[kMaxDays];
  uint32_t days;
  uint32_t bootFingerprint;
  uint32_t petSeed;  // IdentityRecord::seed
  uint32_t shifts, decays, sessions, evidence;
  TemperamentState genome;  // end of life
  TemperamentState board;   // the board core's own genome (what really leans choices)
};

struct Run {
  const char* name;
  OwnerConfig owner;
  uint64_t ownerSeed;    // owner timing and reactions
  uint64_t entropySeed;  // BoardLab entropy (PetId at migration)
  uint32_t days;
  uint32_t absentFrom;   // owner stops coming from this local day
  uint32_t badDay;       // owner shoos everything on this local day
  ShadowConfig shadow;
  LeaningConfig leaning;  // PE-4a: the board's defaults unless a sweep changes them
};

// Owner preference profiles per interaction (quiet, play, greeting);
// learner index 0 idle, 1 happy, 2 curious.
const uint8_t kProfiles[3][kInteractions] = {{0, 0, 1}, {2, 0, 2}, {1, 0, 0}};

OwnerConfig owner(OwnerKind kind, uint8_t profile, uint8_t later = 2) {
  OwnerConfig c;
  c.kind = kind;
  c.contexts = kInteractions;
  c.actions = kActions;
  c.rateQ16 = 19661;  // reacts to 30 % of choices while present [P]
  for (uint8_t i = 0; i < kInteractions; ++i) {
    c.preferred[i] = kProfiles[profile][i];
    c.laterPreferred[i] = kProfiles[later][i];
  }
  return c;
}

Run run(const char* name, const OwnerConfig& o, uint64_t seed) {
  Run r;
  r.name = name;
  r.owner = o;
  r.ownerSeed = seed;
  r.entropySeed = seed;
  r.days = kDefaultDays;
  r.absentFrom = kNever;
  r.badDay = kNever;
  return r;
}

void greedyInto(const BoardLab& lab, Day& d) {
  for (uint8_t i = 0; i < kInteractions; ++i)
    for (uint8_t p = 0; p < kParts; ++p)
      d.greedy[i][p] = lab.core().learner().greedy(i, p);
  d.fingerprint = lab.core().learner().fingerprint();
}

void endDay(const BoardLab& lab, const PersonalityShadow& shadow, Day& d) {
  greedyInto(lab, d);
  for (uint8_t i = 0; i < kTraitCount; ++i)
    d.trait[i] = Temperament::effective(shadow.genome(), static_cast<TraitId>(i));
}

// A decision XUNA showed: count it, and what the shadow would have shown.
void count(const LifeCoreStepOutput& out, const BoardLab& lab, PersonalityShadow& shadow, Day& d) {
  uint8_t action = 0;
  if (!learnIndexFor(static_cast<ActionId>(out.selectedAction), action)) return;
  if (out.decisionContext >= kInteractions || out.decisionDayPart >= kParts) return;
  ++d.shown[out.decisionContext][out.decisionDayPart][action];
  ++d.decisions;
  shadow.shown(out, lab.core().learner());
  const uint8_t alt = shadow.choose(out.decisionContext, out.decisionDayPart, lab.core().learner());
  ++d.shadow[out.decisionContext][out.decisionDayPart][alt];
  shadow.noteShadowAction(alt);
}

bool live(const Run& spec, Life& life) {
  life = Life();
  life.days = spec.days;
  BoardLab lab(spec.entropySeed);
  lab.setLeaning(spec.leaning);
  lab.store().seedV1(xuna_fixture::fresh, sizeof(xuna_fixture::fresh));
  if (!lab.boot(0)) return false;
  life.bootFingerprint = lab.core().learner().fingerprint();
  life.petSeed = lab.identity().record().seed;
  PersonalityShadow shadow(life.petSeed, spec.shadow);
  shadow.begin(0);
  const OwnerModel model(spec.owner);
  Pcg32 rng(spec.ownerSeed, 17);
  const uint32_t baseLocal = (kUtcBase - 1577836800U) / 60 + uint32_t(kTz);
  const uint32_t firstDay = baseLocal / 1440;
  const uint64_t end = (uint64_t(spec.days) * 1440 - baseLocal % 1440) * kMinuteMs;
  uint32_t planned = kNever;
  uint32_t window[2][2] = {};  // [morning, evening] × [start, end) local minutes
  uint64_t reactAt = 0;
  bool reactPet = false;
  uint32_t id = 0;
  uint8_t frame[16];
  for (uint64_t t = 1000; t < end; t += 1000) {
    const uint32_t now = uint32_t(t);
    const uint32_t local = baseLocal + uint32_t(t / kMinuteMs);
    const uint32_t d = local / 1440 - firstDay;
    const uint32_t minute = local % 1440;
    if (d != planned) {
      if (planned != kNever) endDay(lab, shadow, life.day[planned]);
      planned = d;
      life.day[d].moodMin = 32767;
      life.day[d].moodMax = -32768;
      window[0][0] = 7 * 60 - 30 + rng.below(61);
      window[0][1] = window[0][0] + 30;
      window[1][0] = 19 * 60 - 30 + rng.below(61);
      window[1][1] = window[1][0] + 120;
    }
    Day& day = life.day[d];
    const bool away = d >= spec.absentFrom;
    bool present = false;
    for (uint8_t w = 0; w < 2; ++w)
      present = present || (minute >= window[w][0] && minute < window[w][1]);
    present = present && !away;
    const bool onMinute = t % kMinuteMs == 0;
    if (present && onMinute) {
      const bool arrival = minute == window[0][0] || minute == window[1][0];
      if (arrival)
        lab.command(frame, BoardLab::timeSync(frame, ++id, kUtcBase + uint32_t(t / 1000), kTz),
                    now);
      if (minute % 15 == 0 && !reactAt && !lab.core().reactionOpen(now)) {
        shadow.ownerInput(now);
        if (lab.greeting(now)) {
          const LifeCoreStepOutput& out = lab.adapter().last();
          count(out, lab, shadow, day);
          uint8_t shown = 0;
          learnIndexFor(static_cast<ActionId>(out.selectedAction), shown);
          if (d == spec.badDay || model.rates(rng)) {
            reactAt = t + 1000 + uint64_t(rng.below(3000));
            reactPet = d != spec.badDay && model.approves(d, out.decisionContext, shown, rng);
          }
        }
      }
    }
    // A pet is GPIO0: only inside the reaction window, or it would be a greeting.
    if (reactAt && t >= reactAt && lab.core().reactionOpen(now)) {
      shadow.ownerInput(now);
      if (reactPet) lab.greeting(now);
      else lab.idleButton(now);
      if (lab.adapter().lastReactionApplied()) {
        ++(reactPet ? day.pets : day.shoos);
        shadow.reaction(reactPet);
      }
      reactAt = 0;
    }
    lab.loop(now);
    const LifeCoreStepOutput& out = lab.adapter().last();
    shadow.step(now, out);
    if (onMinute) {
      if (out.activity == Activity::Resting || out.activity == Activity::Sleeping) ++day.restMinutes;
      const int16_t mood = shadow.affect().state().moodValence;
      day.moodSum += mood;
      ++day.moodSamples;
      if (mood < day.moodMin) day.moodMin = mood;
      if (mood > day.moodMax) day.moodMax = mood;
    }
    if (out.decisionShown) {
      count(out, lab, shadow, day);
      reactAt = 0;
      if (present && (d == spec.badDay || model.rates(rng))) {
        uint8_t shown = 0;
        learnIndexFor(static_cast<ActionId>(out.selectedAction), shown);
        reactAt = t + 1000 + uint64_t(rng.below(3000));
        reactPet = d != spec.badDay && model.approves(d, out.decisionContext, shown, rng);
      }
    }
    if (lab.checkpointFailures() || out.activity == Activity::ErrorSafe) return false;
  }
  endDay(lab, shadow, life.day[planned]);
  life.shifts = shadow.shifts();
  life.decays = shadow.decays();
  life.sessions = shadow.sessions();
  life.evidence = shadow.evidence();
  life.genome = shadow.genome();
  life.board = lab.core().personality().genome();
  return true;
}

// ---- metrics (host doubles; never part of xuna_core) ----

// The learner's side of a day (what XUNA really showed), not the shadow.
bool sameLearnerDay(const Day& a, const Day& b) {
  return std::memcmp(a.shown, b.shown, sizeof(a.shown)) == 0 &&
         std::memcmp(a.greedy, b.greedy, sizeof(a.greedy)) == 0 &&
         a.fingerprint == b.fingerprint && a.decisions == b.decisions && a.pets == b.pets &&
         a.shoos == b.shoos;
}

// `shadow`: what the PE-1 shadow would have shown instead of what XUNA showed.
uint32_t cell(const Life& l, uint32_t from, uint32_t to, uint8_t i, uint8_t p, double out[kActions],
              bool shadow = false) {
  uint32_t n[kActions] = {};
  uint32_t total = 0;
  for (uint32_t d = from; d <= to && d < l.days; ++d)
    for (uint8_t a = 0; a < kActions; ++a)
      n[a] += shadow ? l.day[d].shadow[i][p][a] : l.day[d].shown[i][p][a];
  for (uint8_t a = 0; a < kActions; ++a) total += n[a];
  for (uint8_t a = 0; a < kActions; ++a) out[a] = total ? double(n[a]) / total : 0;
  return total;
}

double js(const double* p, const double* q) {
  double r = 0;
  for (uint8_t a = 0; a < kActions; ++a) {
    const double m = (p[a] + q[a]) / 2;
    if (p[a] > 0) r += p[a] * std::log2(p[a] / m) / 2;
    if (q[a] > 0) r += q[a] * std::log2(q[a] / m) / 2;
  }
  return r;
}

struct Distance {
  double value;
  uint32_t cells;
};

// Mean JS over the compared cells that both windows sampled ≥ minSamples times.
Distance distance(const Life& a, uint32_t af, uint32_t at, const Life& b, uint32_t bf,
                  uint32_t bt, uint32_t minSamples, bool shadow = false) {
  Distance r = {0, 0};
  for (uint8_t i : kCompared)
    for (uint8_t p = 0; p < kParts; ++p) {
      double x[kActions], y[kActions];
      if (cell(a, af, at, i, p, x, shadow) < minSamples ||
          cell(b, bf, bt, i, p, y, shadow) < minSamples)
        continue;
      r.value += js(x, y);
      ++r.cells;
    }
  if (r.cells) r.value /= r.cells;
  return r;
}

// Greedy cells that changed from one day to the next, per day.
double flipsPerDay(const Life& l, uint32_t from, uint32_t to) {
  uint32_t flips = 0;
  for (uint32_t d = from; d <= to; ++d)
    for (uint8_t i : kCompared)
      for (uint8_t p = 0; p < kParts; ++p)
        if (l.day[d].greedy[i][p] != l.day[d - 1].greedy[i][p]) ++flips;
  return double(flips) / (to - from + 1);
}

// Share of the 8 fine cells (quiet/greeting × morning…night) whose greedy
// choice is the owner's preferred action on that day.
double match(const Life& l, uint32_t d, const OwnerModel& m) {
  uint32_t hits = 0;
  for (uint8_t i : kCompared)
    for (uint8_t p = 1; p < kParts; ++p)
      if (l.day[d].greedy[i][p] == m.bestAction(d, i)) ++hits;
  return hits / 8.0;
}

uint32_t firstDayMatching(const Life& l, const OwnerModel& m, uint32_t from, double share) {
  for (uint32_t d = from; d < l.days; ++d)
    if (match(l, d, m) >= share) return d;
  return kNever;
}

double sharePreferred(const Life& l, uint32_t from, uint32_t to, uint8_t i, uint8_t action) {
  uint32_t n = 0, total = 0;
  for (uint32_t d = from; d <= to; ++d)
    for (uint8_t p = 0; p < kParts; ++p)
      for (uint8_t a = 0; a < kActions; ++a) {
        total += l.day[d].shown[i][p][a];
        if (a == action) n += l.day[d].shown[i][p][a];
      }
  return total ? double(n) / total : 0;
}

// ---- PE-1 personality metrics ----

// Traits that receive evidence in PE-1d; the others stay innate (O1 default).
const TraitId kLearnable[] = {TraitId::Curiosity, TraitId::Sociability, TraitId::Playfulness,
                              TraitId::Tempo};

double unit(int32_t q14) { return q14 / 16384.0; }

// Mean |Δ effective trait| over the learnable traits, in trait units (1.0).
double traitGap(const Day& a, const Day& b) {
  double sum = 0;
  for (TraitId t : kLearnable) {
    const int32_t d = int32_t(a.trait[uint8_t(t)]) - b.trait[uint8_t(t)];
    sum += unit(d < 0 ? -d : d);
  }
  return sum / 4;
}

// The first three learnable traits move only with owner reactions; tempo
// follows the owner's session density, which is the same schedule in every
// scenario here, so it is reported apart.
constexpr uint8_t kReactionTraits = 3;

// Mean |learned offset| over the first `count` learnable traits, end of life.
double learnedAbs(const Life& l, uint8_t count) {
  double sum = 0;
  for (uint8_t k = 0; k < count; ++k) {
    const int16_t o = l.genome.offset[uint8_t(kLearnable[k])];
    sum += unit(o < 0 ? -o : o);
  }
  return sum / count;
}
double learnedAbs(const Life& l) { return learnedAbs(l, 4); }
double learnedAbsReaction(const Life& l) { return learnedAbs(l, kReactionTraits); }

double meanOffset(const Life* lives, uint32_t n, TraitId t) {
  double sum = 0;
  for (uint32_t k = 0; k < n; ++k) sum += unit(lives[k].genome.offset[uint8_t(t)]);
  return sum / n;
}

// Mean over the first `count` learnable traits of (max − min) across days.
double traitSwing(const Life& l, uint32_t from, uint32_t to, uint8_t count = 4) {
  double sum = 0;
  for (uint8_t k = 0; k < count; ++k) {
    int16_t lo = 32767, hi = -32768;
    for (uint32_t d = from; d <= to; ++d) {
      const int16_t v = l.day[d].trait[uint8_t(kLearnable[k])];
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    sum += unit(hi - lo);
  }
  return sum / count;
}

// Shadow variety: mean per-cell entropy (bits) and top-action share.
double shadowEntropy(const Life* lives, uint32_t n, double& top) {
  double entropy = 0;
  uint32_t sampled = 0;
  top = 0;
  for (uint32_t k = 0; k < n; ++k)
    for (uint8_t i : kCompared)
      for (uint8_t p = 0; p < kParts; ++p) {
        double x[kActions];
        if (cell(lives[k], kWeekFrom, kWeekTo, i, p, x, true) < 10) continue;
        double h = 0, best = 0;
        for (uint8_t a = 0; a < kActions; ++a) {
          if (x[a] > 0) h -= x[a] * std::log2(x[a]);
          if (x[a] > best) best = x[a];
        }
        entropy += h;
        top += best;
        ++sampled;
      }
  if (sampled) top /= sampled;
  return sampled ? entropy / sampled : -1;
}

// Share of sampled cells whose most frequent shadow action is the learner's
// greedy choice at the end: the genome does not overrule what was learned.
double shadowFollowsLearner(const Life* lives, uint32_t n) {
  uint32_t hits = 0, cells = 0;
  for (uint32_t k = 0; k < n; ++k)
    for (uint8_t i : kCompared)
      for (uint8_t p = 0; p < kParts; ++p) {
        double x[kActions];
        if (cell(lives[k], kWeekFrom, kWeekTo, i, p, x, true) < 10) continue;
        uint8_t best = 0;
        for (uint8_t a = 1; a < kActions; ++a)
          if (x[a] > x[best]) best = a;
        hits += best == lives[k].day[kWeekTo].greedy[i][p];
        ++cells;
      }
  return cells ? double(hits) / cells : -1;
}

double moodMean(const Life& l, uint32_t from, uint32_t to) {
  int64_t sum = 0;
  uint32_t n = 0;
  for (uint32_t d = from; d <= to; ++d) {
    sum += l.day[d].moodSum;
    n += l.day[d].moodSamples;
  }
  return n ? unit(int32_t(sum / n)) : 0;
}

// PE-2b: XUNA's own (quiet-context) choices and rest per day, days from..to.
double quietPerDay(const Life& l, uint32_t from, uint32_t to) {
  uint64_t n = 0;
  for (uint32_t d = from; d <= to; ++d)
    for (uint8_t p = 0; p < kParts; ++p)
      for (uint8_t a = 0; a < kActions; ++a) n += l.day[d].shown[0][p][a];
  return double(n) / (to - from + 1);
}

double restPerDay(const Life& l, uint32_t from, uint32_t to) {
  uint64_t n = 0;
  for (uint32_t d = from; d <= to; ++d) n += l.day[d].restMinutes;
  return double(n) / (to - from + 1);
}

double moodLow(const Life& l, uint32_t from, uint32_t to) {
  int16_t low = 32767;
  for (uint32_t d = from; d <= to; ++d)
    if (l.day[d].moodMin < low) low = l.day[d].moodMin;
  return unit(low);
}

double meanOverLives(const Life* lives, uint32_t n, double (*f)(const Life&)) {
  double sum = 0;
  for (uint32_t k = 0; k < n; ++k) sum += f(lives[k]);
  return sum / n;
}

// ---- reporting ----

constexpr uint32_t kSeeds = 5;
Life g_lives[16];

void printRun(const Run& r, const Life& l) {
  uint32_t decisions = 0, pets = 0, shoos = 0;
  for (uint32_t d = 0; d < l.days; ++d) {
    decisions += l.day[d].decisions;
    pets += l.day[d].pets;
    shoos += l.day[d].shoos;
  }
  const Day& last = l.day[l.days - 1];
  std::printf("run,%s,%u,%llu,%llu,%u,%u,%u,", r.name, unsigned(r.owner.kind),
              static_cast<unsigned long long>(r.ownerSeed),
              static_cast<unsigned long long>(r.entropySeed), decisions, pets, shoos);
  for (uint8_t i : kCompared)
    for (uint8_t p = 0; p < kParts; ++p) std::printf("%u", last.greedy[i][p]);
  std::printf(",%08x\n", last.fingerprint);
}

bool liveAll(const Run* runs, uint32_t n, Life* lives) {
  for (uint32_t k = 0; k < n; ++k) {
    if (!live(runs[k], lives[k])) {
      std::printf("FAIL,%s,seed=%llu\n", runs[k].name,
                  static_cast<unsigned long long>(runs[k].ownerSeed));
      return false;
    }
    printRun(runs[k], lives[k]);
  }
  return true;
}

void pe1(const char* scenario, const char* name, double value, const char* note = "") {
  std::printf("pe1,%s,%s,%.4f,%s\n", scenario, name, value, note);
}

void metric(const char* scenario, const char* name, double value, const char* note = "") {
  std::printf("metric,%s,%s,%.4f,%s\n", scenario, name, value, note);
}

void pe2(const char* scenario, const char* name, double value, const char* note = "") {
  std::printf("pe2,%s,%s,%.4f,%s\n", scenario, name, value, note);
}

void pe4(const char* scenario, const char* name, double value, const char* note = "") {
  std::printf("pe4,%s,%s,%.4f,%s\n", scenario, name, value, note);
}

// Mean and spread (max − min) over a group of lives.
void pe2Rhythm(const char* scenario, const Life* lives, uint32_t count) {
  double q = 0, r = 0, qLo = 1e9, qHi = -1e9;
  for (uint32_t i = 0; i < count; ++i) {
    const double qi = quietPerDay(lives[i], kWeekFrom, kWeekTo);
    q += qi;
    r += restPerDay(lives[i], kWeekFrom, kWeekTo);
    if (qi < qLo) qLo = qi;
    if (qi > qHi) qHi = qi;
  }
  pe2(scenario, "own_choices_per_day", q / count, "quiet-context decisions, days 23-29");
  pe2(scenario, "own_choices_spread_pct", q > 0 ? 100.0 * (qHi - qLo) / (q / count) : 0,
      "max - min across pets, % of mean");
  pe2(scenario, "rest_minutes_per_day", r / count, "Resting + Sleeping, days 23-29");
}


bool ownersVersusSeeds() {
  static Life lives[3][kSeeds];
  for (uint8_t profile = 0; profile < 3; ++profile)
    for (uint32_t s = 0; s < kSeeds; ++s) {
      const Run r = run(profile == 0 ? "owners_p0" : profile == 1 ? "owners_p1" : "owners_p2",
                        owner(OwnerKind::Oracle, profile), s + 1);
      if (!liveAll(&r, 1, &lives[profile][s])) return false;
    }
  double between = 0, within = 0, greedyBetween = 0, greedyWithin = 0;
  uint32_t nb = 0, nw = 0;
  for (uint32_t s = 0; s < kSeeds; ++s)
    for (uint8_t a = 0; a < 3; ++a)
      for (uint8_t b = a + 1; b < 3; ++b) {
        between += distance(lives[a][s], kWeekFrom, kWeekTo, lives[b][s], kWeekFrom, kWeekTo, 10).value;
        uint32_t diff = 0;
        for (uint8_t i : kCompared)
          for (uint8_t p = 1; p < kParts; ++p)
            diff += lives[a][s].day[kWeekTo].greedy[i][p] != lives[b][s].day[kWeekTo].greedy[i][p];
        greedyBetween += diff / 8.0;
        ++nb;
      }
  for (uint8_t profile = 0; profile < 3; ++profile)
    for (uint32_t a = 0; a < kSeeds; ++a)
      for (uint32_t b = a + 1; b < kSeeds; ++b) {
        within += distance(lives[profile][a], kWeekFrom, kWeekTo, lives[profile][b], kWeekFrom,
                           kWeekTo, 10).value;
        uint32_t diff = 0;
        for (uint8_t i : kCompared)
          for (uint8_t p = 1; p < kParts; ++p)
            diff += lives[profile][a].day[kWeekTo].greedy[i][p] !=
                    lives[profile][b].day[kWeekTo].greedy[i][p];
        greedyWithin += diff / 8.0;
        ++nw;
      }
  between /= nb;
  within /= nw;
  metric("owners", "js_between_owners", between, "same seed, different owner profile, days 23-29");
  metric("owners", "js_within_owner", within, "same owner profile, different seed, days 23-29");
  metric("owners", "ratio_between_over_within", within > 0 ? between / within : 0,
         "plan threshold for PE-1: >= 3 between owners vs within; here the baseline");
  metric("owners", "greedy_differs_between_owners", greedyBetween / nb, "share of 8 fine cells, day 29");
  metric("owners", "greedy_differs_within_owner", greedyWithin / nw, "share of 8 fine cells, day 29");
  double stability = 0, flips = 0;
  for (uint8_t profile = 0; profile < 3; ++profile)
    for (uint32_t s = 0; s < kSeeds; ++s) {
      const Life& l = lives[profile][s];
      double sum = 0;
      for (uint32_t d = kWeekFrom; d <= kWeekTo; ++d)
        sum += distance(l, d - 1, d - 1, l, d, d, 5).value;
      stability += sum / (kWeekTo - kWeekFrom + 1);
      flips += flipsPerDay(l, kWeekFrom, kWeekTo);
    }
  // Variety: entropy of the shown actions in each cell (0 = always the same
  // action, log2 3 = 1.585 = uniform) and the top action's share.
  double entropy = 0, top = 0;
  uint32_t sampled = 0;
  for (uint8_t profile = 0; profile < 3; ++profile)
    for (uint32_t s = 0; s < kSeeds; ++s)
      for (uint8_t i : kCompared)
        for (uint8_t p = 0; p < kParts; ++p) {
          double x[kActions];
          if (cell(lives[profile][s], kWeekFrom, kWeekTo, i, p, x) < 10) continue;
          double h = 0, best = 0;
          for (uint8_t a = 0; a < kActions; ++a) {
            if (x[a] > 0) h -= x[a] * std::log2(x[a]);
            if (x[a] > best) best = x[a];
          }
          entropy += h;
          top += best;
          ++sampled;
        }
  metric("owners", "action_entropy_bits", sampled ? entropy / sampled : -1, "per cell, days 23-29; max 1.585");
  metric("owners", "top_action_share", sampled ? top / sampled : -1, "per cell, days 23-29");
  metric("owners", "js_day_to_day", stability / (3 * kSeeds), "same pet, consecutive days 22-29; sampling floor included");
  metric("owners", "greedy_flips_per_day", flips / (3 * kSeeds), "cells (of 10) whose greedy changed overnight, days 23-29");
  double learned = 0;
  uint32_t never = 0;
  for (uint8_t profile = 0; profile < 3; ++profile)
    for (uint32_t s = 0; s < kSeeds; ++s) {
      const uint32_t d = firstDayMatching(lives[profile][s], OwnerModel(owner(OwnerKind::Oracle, profile)), 1, 0.75);
      if (d == kNever) ++never;
      else learned += d;
    }
  const uint32_t n = 3 * kSeeds - never;
  metric("owners", "first_day_75pct_cells_follow_owner", n ? learned / n : -1, "Oracle, mean over runs that got there");
  metric("owners", "runs_never_75pct", never, "of 15");

  // PE-1 shadow: genome, ledger, drives and affect beside the same lives.
  double gapBetween = 0, gapWithin = 0, jsBetween = 0, jsWithin = 0;
  for (uint32_t s = 0; s < kSeeds; ++s)
    for (uint8_t a = 0; a < 3; ++a)
      for (uint8_t b = a + 1; b < 3; ++b) {
        gapBetween += traitGap(lives[a][s].day[kWeekTo], lives[b][s].day[kWeekTo]);
        jsBetween += distance(lives[a][s], kWeekFrom, kWeekTo, lives[b][s], kWeekFrom, kWeekTo, 10, true).value;
      }
  for (uint8_t profile = 0; profile < 3; ++profile)
    for (uint32_t a = 0; a < kSeeds; ++a)
      for (uint32_t b = a + 1; b < kSeeds; ++b) {
        gapWithin += traitGap(lives[profile][a].day[kWeekTo], lives[profile][b].day[kWeekTo]);
        jsWithin += distance(lives[profile][a], kWeekFrom, kWeekTo, lives[profile][b], kWeekFrom,
                             kWeekTo, 10, true).value;
      }
  gapBetween /= nb;
  gapWithin /= nw;
  pe1("owners", "trait_gap_between_owners", gapBetween, "same seed, different owner; learnable traits, day 29");
  pe1("owners", "trait_gap_within_owner", gapWithin, "same owner, different seed (innate + learned)");
  pe1("owners", "trait_ratio_between_over_within", gapWithin > 0 ? gapBetween / gapWithin : 0,
      "plan 5 row 1 at trait level: >= 3");
  pe1("owners", "shadow_js_between_owners", jsBetween / nb);
  pe1("owners", "shadow_js_within_owner", jsWithin / nw, "visible individuality under one owner");
  const char* traitNames[] = {"curiosity", "sociability", "playfulness", "tempo"};
  for (uint8_t profile = 0; profile < 3; ++profile)
    for (uint8_t k = 0; k < 4; ++k) {
      char name[48];
      std::snprintf(name, sizeof(name), "p%u_offset_%s", profile, traitNames[k]);
      pe1("owners", name, meanOffset(lives[profile], kSeeds, kLearnable[k]), "learned, day 29, mean of 5");
    }
  double shadowTop = 0;
  pe1("owners", "shadow_entropy_bits", shadowEntropy(&lives[0][0], 3 * kSeeds, shadowTop), "learner alone: 0.091");
  pe1("owners", "shadow_top_action_share", shadowTop, "learner alone: 0.976");
  pe1("owners", "shadow_top_is_learner_greedy", shadowFollowsLearner(&lives[0][0], 3 * kSeeds),
      "trait does not overrule learning");
  double shifts = 0, mood = 0;
  for (uint8_t profile = 0; profile < 3; ++profile)
    for (uint32_t s = 0; s < kSeeds; ++s) {
      shifts += lives[profile][s].shifts;
      mood += moodMean(lives[profile][s], kWeekFrom, kWeekTo);
    }
  pe1("owners", "shifts_per_run", shifts / (3 * kSeeds), "consolidations that moved a trait in 30 days");
  pe1("owners", "mood_mean_last_week", mood / (3 * kSeeds));
  pe2Rhythm("owners", &lives[0][0], 3 * kSeeds);
  return true;
}

bool entropyOnly() {
  Run runs[kSeeds];
  for (uint32_t s = 0; s < kSeeds; ++s) {
    runs[s] = run("entropy_only", owner(OwnerKind::Oracle, 1), 1);
    runs[s].entropySeed = s + 1;
  }
  if (!liveAll(runs, kSeeds, g_lives)) return false;
  uint32_t identical = 0;
  double sum = 0;
  for (uint32_t s = 1; s < kSeeds; ++s) {
    bool same = true;
    for (uint32_t d = 0; d < g_lives[0].days; ++d)
      same = same && sameLearnerDay(g_lives[0].day[d], g_lives[s].day[d]);
    identical += same;
    sum += distance(g_lives[0], kWeekFrom, kWeekTo, g_lives[s], kWeekFrom, kWeekTo, 10).value;
  }
  metric("entropy_only", "lives_identical_to_seed1", identical, "of 4: same owner draws, only the PetId entropy differs");
  metric("entropy_only", "js_mean", sum / (kSeeds - 1));
  double gap = 0, shadowJs = 0;
  for (uint32_t s = 1; s < kSeeds; ++s) {
    gap += traitGap(g_lives[0].day[kWeekTo], g_lives[s].day[kWeekTo]);
    shadowJs += distance(g_lives[0], kWeekFrom, kWeekTo, g_lives[s], kWeekFrom, kWeekTo, 10, true).value;
  }
  pe1("entropy_only", "trait_gap", gap / (kSeeds - 1), "learnable traits, day 29");
  pe1("entropy_only", "shadow_js", shadowJs / (kSeeds - 1),
      "same owner draws, different IdentityRecord.seed; proposed G1 >= 0.1 (learner alone: 0)");
  // Expression channels that do not touch the choice (PE-2 candidates):
  // spread (max − min) across the five pets.
  double lo[4] = {1e9, 1e9, 1e9, 1e9}, hi[4] = {-1e9, -1e9, -1e9, -1e9};
  for (uint32_t s = 0; s < kSeeds; ++s) {
    const Life& l = g_lives[s];
    const double v[4] = {
        15.0 * unit(l.day[kWeekTo].trait[uint8_t(TraitId::Tempo)]),  // frame speed ±15 % [P]
        unit(Affect::parametersFor(l.genome).gain),                  // emotion size
        double(Drives::ratesFor(l.genome).explore),                  // explore rise / min
        moodMean(l, kWeekFrom, kWeekTo)};
    for (uint8_t k = 0; k < 4; ++k) {
      if (v[k] < lo[k]) lo[k] = v[k];
      if (v[k] > hi[k]) hi[k] = v[k];
    }
  }
  pe1("entropy_only", "spread_frame_speed_pct", hi[0] - lo[0], "tempo x 15 %, day 29");
  pe1("entropy_only", "spread_emotion_gain", hi[1] - lo[1], "neutral 0.5");
  pe1("entropy_only", "spread_explore_rate_pct", 100.0 * (hi[2] - lo[2]) / 512, "of the neutral rate");
  pe1("entropy_only", "spread_mood_mean", hi[3] - lo[3], "last week");
  pe2Rhythm("entropy_only", g_lives, kSeeds);
  return true;
}

bool absence() {
  Run runs[kSeeds];
  for (uint32_t s = 0; s < kSeeds; ++s) {
    runs[s] = run("absence", owner(OwnerKind::Oracle, 1), s + 1);
    runs[s].absentFrom = 14;
  }
  if (!liveAll(runs, kSeeds, g_lives)) return false;
  double js1 = 0, cells = 0;
  uint32_t learnerSame = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    const Life& l = g_lives[s];
    learnerSame += l.day[14].fingerprint == l.day[29].fingerprint;
    const Distance d = distance(l, 7, 13, l, 23, 29, 10);
    js1 += d.value;
    cells += d.cells;
  }
  metric("absence", "learner_unchanged_days_14_to_29", learnerSame, "of 5: fingerprint equal (owner gone from day 14)");
  metric("absence", "js_week2_vs_week5", js1 / kSeeds,
         "no reboot: the clock keeps its day parts after the last sync, so the fine cells stay in use");
  metric("absence", "cells_compared", cells / kSeeds, "greeting cells drop out: nobody presses GPIO0");
  double gap = 0, mood = 0, low = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    gap += traitGap(g_lives[s].day[13], g_lives[s].day[29]);
    mood += moodMean(g_lives[s], 23, 29) - unit(Affect::parametersFor(g_lives[s].genome).prevalence);
    low += moodLow(g_lives[s], 15, 29);
  }
  pe1("absence", "trait_change_days_13_to_29", gap / kSeeds, "plan 5: 0");
  pe1("absence", "mood_minus_baseline_last_week", mood / kSeeds, "no sadness from absence: ~0");
  pe1("absence", "mood_low_days_15_29", low / kSeeds);
  return true;
}

bool driftOwners(OwnerKind kind, const char* name) {
  Run runs[kSeeds];
  for (uint32_t s = 0; s < kSeeds; ++s) runs[s] = run(name, owner(kind, 1), s + 1);
  if (!liveAll(runs, kSeeds, g_lives)) return false;
  double flips = 0, weeks = 0, followed = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    flips += flipsPerDay(g_lives[s], 8, 29);
    weeks += distance(g_lives[s], 1, 7, g_lives[s], 23, 29, 10).value;
    followed += match(g_lives[s], 29, OwnerModel(owner(kind, 1)));
  }
  metric(name, "greedy_flips_per_day", flips / kSeeds, "days 8-29");
  metric(name, "js_week1_vs_week4", weeks / kSeeds);
  metric(name, "cells_following_profile_day29", followed / kSeeds,
         kind == OwnerKind::Random ? "no real preference: chance level only" : "");
  double swing = 0, shifts = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    swing += traitSwing(g_lives[s], 8, 29, kReactionTraits);
    shifts += g_lives[s].shifts;
  }
  pe1(name, "reaction_learned_abs", meanOverLives(g_lives, kSeeds, learnedAbsReaction),
      "mean |offset| of curiosity, sociability, playfulness, day 29");
  pe1(name, "reaction_trait_swing_days_8_29", swing / kSeeds);
  pe1(name, "tempo_offset", meanOffset(g_lives, kSeeds, TraitId::Tempo), "session density, same schedule as every owner");
  pe1(name, "shifts_per_run", shifts / kSeeds);
  return true;
}

bool silent() {
  Run runs[kSeeds];
  for (uint32_t s = 0; s < kSeeds; ++s) runs[s] = run("silent", owner(OwnerKind::NoFeedback, 1), s + 1);
  if (!liveAll(runs, kSeeds, g_lives)) return false;
  uint32_t same = 0;
  double weeks = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    same += g_lives[s].day[29].fingerprint == g_lives[s].bootFingerprint;
    weeks += distance(g_lives[s], 1, 7, g_lives[s], 23, 29, 10).value;
  }
  metric("silent", "learner_equal_to_boot_day29", same, "of 5");
  metric("silent", "js_week1_vs_week4", weeks / kSeeds);
  pe2Rhythm("silent", g_lives, kSeeds);
  double reactionTraits = 0;
  for (uint32_t s = 0; s < kSeeds; ++s)
    for (uint8_t k = 0; k < 3; ++k) {  // curiosity, sociability, playfulness
      const int16_t o = g_lives[s].genome.offset[uint8_t(kLearnable[k])];
      reactionTraits += unit(o < 0 ? -o : o);
    }
  pe1("silent", "reaction_traits_abs_offset", reactionTraits / (3 * kSeeds), "plan 5: 0 (no reactions)");
  pe1("silent", "tempo_offset", meanOffset(g_lives, kSeeds, TraitId::Tempo),
      "session density is real owner behaviour (device observed)");
  return true;
}

bool badDay() {
  Run runs[kSeeds];
  for (uint32_t s = 0; s < kSeeds; ++s) {
    runs[s] = run("bad_day", owner(OwnerKind::Oracle, 1), s + 1);
    runs[s].badDay = 20;
  }
  if (!liveAll(runs, kSeeds, g_lives)) return false;
  double changed = 0, shift = 0, recovery = 0;
  uint32_t never = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    const Life& l = g_lives[s];
    const OwnerModel m(owner(OwnerKind::Oracle, 1));
    changed += match(l, 19, m) - match(l, 20, m);
    shift += distance(l, 13, 19, l, 21, 21, 5).value;
    const uint32_t back = firstDayMatching(l, m, 21, match(l, 19, m));
    if (back == kNever) ++never;
    else recovery += back - 20;
  }
  const uint32_t n = kSeeds - never;
  metric("bad_day", "cells_lost_after_one_day", changed / kSeeds,
         "share of 8 fine cells no longer following the owner after one day of shoos (day 20)");
  metric("bad_day", "js_week_before_vs_day_after", shift / kSeeds);
  metric("bad_day", "days_to_recover", n ? recovery / n : -1, "back to day-19 match share");
  metric("bad_day", "runs_never_recovered", never, "of 5");
  double gap21 = 0, low = 0, before = 0, after = 0, normal = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    gap21 += traitGap(g_lives[s].day[19], g_lives[s].day[21]);
    low += moodLow(g_lives[s], 20, 20);
    normal += moodLow(g_lives[s], 13, 19);
    before += moodMean(g_lives[s], 13, 19);
    after += moodMean(g_lives[s], 22, 29);
  }
  pe1("bad_day", "trait_change_day19_to_day21", gap21 / kSeeds, "plan 5: 0");
  pe1("bad_day", "mood_low_on_bad_day", low / kSeeds, "floor -0.5");
  pe1("bad_day", "mood_low_normal_week", normal / kSeeds);
  pe1("bad_day", "mood_mean_week_before", before / kSeeds);
  pe1("bad_day", "mood_mean_days_22_29", after / kSeeds, "recovered");
  return true;
}

bool alternating() {
  Run runs[kSeeds];
  for (uint32_t s = 0; s < kSeeds; ++s) {
    OwnerConfig o = owner(OwnerKind::Alternating, 1, 0);
    o.period = 2;  // days
    runs[s] = run("alternating", o, s + 1);
  }
  if (!liveAll(runs, kSeeds, g_lives)) return false;
  double flips = 0, swing = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    flips += flipsPerDay(g_lives[s], 10, 29);
    double sum = 0;
    uint32_t n = 0;
    for (uint32_t d = 10; d + 3 <= 29; d += 2) {
      sum += distance(g_lives[s], d, d + 1, g_lives[s], d + 2, d + 3, 5).value;
      ++n;
    }
    swing += sum / n;
  }
  metric("alternating", "greedy_flips_per_day", flips / kSeeds, "owner swaps P1/P0 every 2 days, days 10-29");
  metric("alternating", "js_between_2day_blocks", swing / kSeeds, "oscillation amplitude");
  double traitSwings = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) traitSwings += traitSwing(g_lives[s], 10, 29, kReactionTraits);
  pe1("alternating", "reaction_trait_swing_days_10_29", traitSwings / kSeeds, "plan 5: ~0");
  pe1("alternating", "learned_abs", meanOverLives(g_lives, kSeeds, learnedAbs));
  return true;
}

bool reversal() {
  Run runs[kSeeds];
  for (uint32_t s = 0; s < kSeeds; ++s) {
    OwnerConfig o = owner(OwnerKind::Reversal, 1, 2);
    o.changeAt = 15;  // days
    runs[s] = run("reversal", o, s + 1);
  }
  if (!liveAll(runs, kSeeds, g_lives)) return false;
  double latency = 0, half = 0;
  uint32_t never = 0, neverHalf = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    const OwnerModel m(runs[s].owner);
    const uint32_t d = firstDayMatching(g_lives[s], m, 15, 0.75);
    if (d == kNever) ++never;
    else latency += d - 15;
    uint32_t h = kNever;
    for (uint32_t day = 15; day < 30 && h == kNever; ++day)
      if (sharePreferred(g_lives[s], day, day, 0, kProfiles[2][0]) >= 0.5) h = day;
    if (h == kNever) ++neverHalf;
    else half += h - 15;
  }
  metric("reversal", "days_to_75pct_cells_new_preference", kSeeds - never ? latency / (kSeeds - never) : -1,
         "owner switches P1 -> P2 on day 15; personality must be >= 5x slower (plan 5)");
  metric("reversal", "runs_never_75pct", never, "of 5");
  metric("reversal", "days_until_half_of_quiet_choices_new", kSeeds - neverHalf ? half / (kSeeds - neverHalf) : -1);
  metric("reversal", "runs_never_half", neverHalf, "of 5");
  // Curiosity was rewarded for 15 days (P1 likes curious), then happy is.
  double gained = 0, afterwards = 0, play = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    const Life& l = g_lives[s];
    const uint8_t c = uint8_t(TraitId::Curiosity), p = uint8_t(TraitId::Playfulness);
    gained += unit(l.day[14].trait[c] - l.day[0].trait[c]);
    afterwards += unit(l.day[29].trait[c] - l.day[14].trait[c]);
    play += unit(l.day[29].trait[p] - l.day[14].trait[p]);
  }
  pe1("reversal", "curiosity_gain_days_0_14", gained / kSeeds);
  pe1("reversal", "curiosity_change_days_14_29", afterwards / kSeeds,
      "learning reversed in 3.6 days; personality should lag far behind");
  pe1("reversal", "playfulness_change_days_14_29", play / kSeeds);
  return true;
}

bool learningSpeed(OwnerKind kind, const char* name) {
  Run runs[kSeeds];
  for (uint32_t s = 0; s < kSeeds; ++s) runs[s] = run(name, owner(kind, 1), s + 1);
  if (!liveAll(runs, kSeeds, g_lives)) return false;
  double first = 0, final = 0;
  uint32_t never = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    const OwnerModel m(runs[s].owner);
    const uint32_t d = firstDayMatching(g_lives[s], m, 1, 0.75);
    if (d == kNever) ++never;
    else first += d;
    final += match(g_lives[s], 29, m);
  }
  metric(name, "first_day_75pct_cells_follow_owner", kSeeds - never ? first / (kSeeds - never) : -1);
  metric(name, "runs_never_75pct", never, "of 5");
  metric(name, "cells_following_owner_day29", final / kSeeds);
  double evidence = 0, shifts = 0;
  for (uint32_t s = 0; s < kSeeds; ++s) {
    evidence += g_lives[s].evidence;
    shifts += g_lives[s].shifts;
  }
  pe1(name, "learned_abs", meanOverLives(g_lives, kSeeds, learnedAbs));
  pe1(name, "curiosity_offset", meanOffset(g_lives, kSeeds, TraitId::Curiosity));
  pe1(name, "evidence_per_run", evidence / kSeeds);
  pe1(name, "shifts_per_run", shifts / kSeeds);
  return true;
}

// 90-day lives: does the owner's effect on traits overtake the innate one?
bool longOwners() {
  static Life lives[3][kSeeds];
  for (uint8_t profile = 0; profile < 3; ++profile)
    for (uint32_t s = 0; s < kSeeds; ++s) {
      Run r = run("long_owners", owner(OwnerKind::Oracle, profile), s + 1);
      r.days = 90;
      if (!live(r, lives[profile][s])) return false;
    }
  const uint32_t days[] = {29, 59, 89};
  for (uint32_t d : days) {
    double between = 0, within = 0;
    uint32_t nb = 0, nw = 0;
    for (uint32_t s = 0; s < kSeeds; ++s)
      for (uint8_t a = 0; a < 3; ++a)
        for (uint8_t b = a + 1; b < 3; ++b, ++nb)
          between += traitGap(lives[a][s].day[d], lives[b][s].day[d]);
    for (uint8_t profile = 0; profile < 3; ++profile)
      for (uint32_t a = 0; a < kSeeds; ++a)
        for (uint32_t b = a + 1; b < kSeeds; ++b, ++nw)
          within += traitGap(lives[profile][a].day[d], lives[profile][b].day[d]);
    char name[48];
    std::snprintf(name, sizeof(name), "trait_ratio_day%u", d);
    pe1("long_owners", name, (between / nb) / (within / nw), "between owners / within owner; plan >= 3");
    std::snprintf(name, sizeof(name), "trait_gap_between_day%u", d);
    pe1("long_owners", name, between / nb);
    std::snprintf(name, sizeof(name), "trait_gap_within_day%u", d);
    pe1("long_owners", name, within / nw);
  }
  pe1("long_owners", "p1_curiosity_day89", meanOffset(lives[1], kSeeds, TraitId::Curiosity));
  pe1("long_owners", "p2_playfulness_day89", meanOffset(lives[2], kSeeds, TraitId::Playfulness));
  pe1("long_owners", "p0_sociability_day89", meanOffset(lives[0], kSeeds, TraitId::Sociability));
  // PE-4a transfer: in a situation nobody taught (the play context, never
  // used here), how often each pet would show idle / happy / curious at day
  // 89, from its genome alone (Leaning, board defaults).
  const char* actionNames[] = {"idle", "happy", "curious"};
  for (uint8_t profile = 0; profile < 3; ++profile) {
    double share[kActions] = {};
    for (uint32_t s = 0; s < kSeeds; ++s) {
      const V3Leaning lean = Leaning::forChoice(lives[profile][s].board,
                                                LearnInteraction::PlayRequest, LeaningConfig());
      const double total = double(lean.weightQ8[0]) + lean.weightQ8[1] + lean.weightQ8[2];
      for (uint8_t a = 0; a < kActions; ++a) share[a] += lean.weightQ8[a] / total / kSeeds;
    }
    for (uint8_t a = 0; a < kActions; ++a) {
      char name[48];
      std::snprintf(name, sizeof(name), "p%u_untaught_%s_share_day89", profile, actionNames[a]);
      pe4("long_owners", name, share[a], a == 0 ? "untaught cell, 1/3 = no lean" : "");
    }
  }
  return true;
}

// Ledger thresholds: signal from a consistent owner vs drift from noise.
bool sweepLedger() {
  const uint8_t sessions[] = {3, 4, 5};
  const int32_t coherence[] = {8192, 9830, 12288};  // 0.5, 0.6, 0.75
  std::printf("# sweep,<min sessions>,<coherence>,<random reaction learned_abs>,<random reaction swing>,"
              "<p1 curiosity>,<p1 sociability>,<p1 learned_abs>,<alternating swing>\n");
  for (uint8_t ms : sessions)
    for (int32_t co : coherence) {
      double random = 0, randomSwing = 0, curiosity = 0, sociability = 0, p1 = 0, alt = 0;
      for (uint32_t s = 0; s < kSeeds; ++s) {
        Run r = run("sweep", owner(OwnerKind::Random, 1), s + 1);
        r.shadow.ledger.minSessions = ms;
        r.shadow.ledger.coherenceQ14 = co;
        if (!live(r, g_lives[0])) return false;
        random += learnedAbsReaction(g_lives[0]);
        randomSwing += traitSwing(g_lives[0], 8, 29, kReactionTraits);
        r.owner = owner(OwnerKind::Oracle, 1);
        if (!live(r, g_lives[0])) return false;
        curiosity += unit(g_lives[0].genome.offset[uint8_t(TraitId::Curiosity)]);
        sociability += unit(g_lives[0].genome.offset[uint8_t(TraitId::Sociability)]);
        p1 += learnedAbs(g_lives[0]);
        r.owner = owner(OwnerKind::Alternating, 1, 0);
        r.owner.period = 2;
        if (!live(r, g_lives[0])) return false;
        alt += traitSwing(g_lives[0], 10, 29, kReactionTraits);
      }
      std::printf("sweep,%u,%.2f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", ms, co / 16384.0, random / kSeeds,
                  randomSwing / kSeeds, curiosity / kSeeds, sociability / kSeeds, p1 / kSeeds,
                  alt / kSeeds);
    }
  return true;
}

// Drive weight in the shadow choice: variety and individuality vs learning.
bool sweepDrives() {
  const uint16_t weights[] = {0, 8192, 16384, 32768};
  std::printf("# drives,<weight>,<shadow entropy>,<top share>,<top is learner greedy>,"
              "<same-owner shadow js>\n");
  for (uint16_t w : weights) {
    for (uint32_t s = 0; s < kSeeds; ++s) {
      Run r = run("drives", owner(OwnerKind::Oracle, 1), 1);
      r.entropySeed = s + 1;  // same owner draws, different pets
      r.shadow.driveWeightQ16 = w;
      if (!live(r, g_lives[s])) return false;
    }
    double top = 0, js = 0;
    const double entropy = shadowEntropy(g_lives, kSeeds, top);
    for (uint32_t s = 1; s < kSeeds; ++s)
      js += distance(g_lives[0], kWeekFrom, kWeekTo, g_lives[s], kWeekFrom, kWeekTo, 10, true).value;
    std::printf("drives,%.3f,%.4f,%.4f,%.4f,%.4f\n", w / 65536.0, entropy, top,
                shadowFollowsLearner(g_lives, kSeeds), js / (kSeeds - 1));
  }
  return true;
}

// ---- PE-4a: the temperament leans real choices (Leaning.h) ----

// Mean pairwise JS of what XUNA really showed, between the `n` lives.
double withinJs(const Life* lives, uint32_t n, uint32_t from, uint32_t to) {
  double sum = 0;
  uint32_t pairs = 0;
  for (uint32_t a = 0; a < n; ++a)
    for (uint32_t b = a + 1; b < n; ++b, ++pairs)
      sum += distance(lives[a], from, to, lives[b], from, to, 10).value;
  return pairs ? sum / pairs : 0;
}

// Same owner draws, different pets (only the PetId entropy differs), so any
// difference in what they show comes from the temperament.
bool sameOwner(OwnerKind kind, uint8_t profile, const LeaningConfig& lean, Life* lives, Run* runs) {
  for (uint32_t s = 0; s < kSeeds; ++s) {
    runs[s] = run("leaning", owner(kind, profile), 1);
    runs[s].entropySeed = s + 1;
    runs[s].leaning = lean;
    if (!live(runs[s], lives[s])) return false;
  }
  return true;
}

struct Follow {
  double firstDay;  // mean first day ≥ 75 % of fine cells follow the owner
  uint32_t never;   // lives that never got there
  double day29;     // mean share of fine cells following on day 29
};

// Learning over `n` × 3 profiles of lives with their own owner draws and pets.
bool follow(OwnerKind kind, const LeaningConfig& lean, uint32_t n, Follow& out) {
  static Life life;
  double sum = 0, final = 0;
  uint32_t got = 0;
  out.never = 0;
  for (uint32_t s = 0; s < n; ++s) {
    for (uint8_t profile = 0; profile < 3; ++profile) {
      Run r = run("leaning", owner(kind, profile), s + 1);
      r.leaning = lean;
      if (!live(r, life)) return false;
      const OwnerModel m(r.owner);
      const uint32_t d = firstDayMatching(life, m, 1, 0.75);
      if (d == kNever) {
        ++out.never;
      } else {
        sum += d;
        ++got;
      }
      final += match(life, 29, m);
    }
  }
  out.firstDay = got ? sum / got : -1;
  out.day29 = final / (3 * n);
  return true;
}

// What each lever adds, and what it costs learning. Individuality = JS
// between pets under the same owner draws (temperament is all that differs).
bool sweepLeaning() {
  struct Variant {
    const char* name;
    uint16_t tie, margin;
  };
  const Variant variants[] = {
      {"off", 0, 0},          {"tie_0.5", 128, 0},           {"tie_1.0", 256, 0},
      {"tie_0.5+margin_0.03", 128, 1966}, {"tie_1.0+margin_0.03", 256, 1966},
  };
  constexpr uint32_t kSpeedRuns = 20;  // × 3 profiles: the speeds are noisy
  std::printf("# leaning,<variant>,<tie gain>,<margin>,"
              "<silent js same owner days 23-29>,<sparse js same owner days 1-7>,"
              "<oracle js same owner days 1-7>,<oracle js same owner days 23-29>,"
              "<oracle first day 75%%>,<oracle never>,<sparse first day 75%%>,<sparse never>,"
              "<noisy first day 75%%>,<noisy never>,<noisy day-29 follow>,"
              "<random learned_abs>,<reversal days to 75%%>,<reversal never>\n");
  static Life lives[kSeeds];
  static Run runs[kSeeds];
  for (const Variant& v : variants) {
    LeaningConfig lean;
    lean.tieGainQ8 = v.tie;
    lean.marginQ16 = v.margin;
    if (!sameOwner(OwnerKind::NoFeedback, 1, lean, lives, runs)) return false;
    const double silentJs = withinJs(lives, kSeeds, kWeekFrom, kWeekTo);
    if (!sameOwner(OwnerKind::Sparse, 1, lean, lives, runs)) return false;
    const double sparseJs = withinJs(lives, kSeeds, 1, 7);
    double oJs1 = 0, oJs4 = 0;
    for (uint8_t profile = 0; profile < 3; ++profile) {
      if (!sameOwner(OwnerKind::Oracle, profile, lean, lives, runs)) return false;
      oJs1 += withinJs(lives, kSeeds, 1, 7) / 3;
      oJs4 += withinJs(lives, kSeeds, kWeekFrom, kWeekTo) / 3;
    }
    Follow oracle, sparse, noisy;
    if (!follow(OwnerKind::Oracle, lean, kSpeedRuns, oracle) ||
        !follow(OwnerKind::Sparse, lean, kSpeedRuns, sparse) ||
        !follow(OwnerKind::Noisy, lean, kSpeedRuns, noisy))
      return false;
    double drift = 0, latency = 0;
    uint32_t reversed = 0, rNever = 0;
    for (uint32_t s = 0; s < 2 * kSeeds; ++s) {
      Run r = run("leaning", owner(OwnerKind::Random, 1), s + 1);
      r.leaning = lean;
      if (!live(r, lives[0])) return false;
      drift += learnedAbsReaction(lives[0]) / (2 * kSeeds);
      OwnerConfig o = owner(OwnerKind::Reversal, 1, 2);
      o.changeAt = 15;
      r = run("leaning", o, s + 1);
      r.leaning = lean;
      if (!live(r, lives[0])) return false;
      const uint32_t d = firstDayMatching(lives[0], OwnerModel(o), 15, 0.75);
      if (d == kNever) {
        ++rNever;
      } else {
        latency += d - 15;
        ++reversed;
      }
    }
    std::printf("leaning,%s,%.2f,%.3f,%.4f,%.4f,%.4f,%.4f,%.2f,%u,%.2f,%u,%.2f,%u,%.3f,"
                "%.4f,%.2f,%u\n",
                v.name, v.tie / 256.0, v.margin / 65536.0, silentJs, sparseJs, oJs1, oJs4, oracle.firstDay, oracle.never, sparse.firstDay,
                sparse.never, noisy.firstDay, noisy.never, noisy.day29, drift,
                reversed ? latency / reversed : -1, rNever);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const auto start = std::chrono::steady_clock::now();
  if (argc > 1 && std::strcmp(argv[1], "leaning") == 0) {  // PE-4a sweep alone
    const bool ok = sweepLeaning();
    const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
    std::fprintf(stderr, "runtime_s=%.1f\n", spent.count());
    return ok ? 0 : 1;
  }
  std::printf("# PE-0 baseline. run,<scenario>,<owner kind>,<owner seed>,<entropy seed>,"
              "<decisions>,<pets>,<shoos>,<day-29 greedy quiet p0-4 greeting p0-4>,<learner fingerprint>\n");
  std::printf("# metric,<scenario>,<name>,<value>,<note>\n");
  std::printf("# pe1,<scenario>,<name>,<value>,<note>   PE-1 shadow beside the same lives\n");
  std::printf("# pe2,<scenario>,<name>,<value>,<note>   PE-2b rhythm: own choices and rest per day\n");
  bool ok = ownersVersusSeeds();
  ok = ok && entropyOnly();
  ok = ok && absence();
  ok = ok && driftOwners(OwnerKind::Random, "random");
  ok = ok && driftOwners(OwnerKind::Noisy, "noisy");
  ok = ok && silent();
  ok = ok && badDay();
  ok = ok && alternating();
  ok = ok && reversal();
  ok = ok && learningSpeed(OwnerKind::Sparse, "sparse");
  ok = ok && learningSpeed(OwnerKind::Teacher, "teacher");
  ok = ok && longOwners();
  ok = ok && sweepLedger();
  ok = ok && sweepDrives();
  ok = ok && sweepLeaning();
  const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
  std::fprintf(stderr, "runtime_s=%.1f\n", spent.count());
  return ok ? 0 : 1;
}
