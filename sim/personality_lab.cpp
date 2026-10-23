// Personality PE-0 baseline (plan docs/roadmap/personality, 2026-10-23).
// How far do XUNAs differ through learning alone, before any temperament?
// Host only, lab only; nothing here runs on the board:
//   c++ -std=c++11 -O2 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/personality_lab.cpp -o personality_lab
//   ./personality_lab > pe0.txt      (deterministic; runtime goes to stderr)
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
#include <xuna/learning/LearnerVocabulary.h>

#include "../test/fixtures/v1_snapshots.h"

namespace {

using namespace xuna;

constexpr uint32_t kMaxDays = 30;
constexpr uint8_t kInteractions = kLearnInteractionCount;
constexpr uint8_t kParts = LearnerV3Config::kMaxDayParts;
constexpr uint8_t kActions = kLearnActionCount;
constexpr uint64_t kMinuteMs = 60000ULL;
constexpr uint64_t kDayMs = 1440 * kMinuteMs;
constexpr uint32_t kUtcBase = 1792026000U;  // 2026-10-15 05:00 UTC = 12:00 local
constexpr int16_t kTz = 420;                // Asia/Ho_Chi_Minh
constexpr uint32_t kNever = 0xFFFFFFFFU;
// Cells compared: quiet (spontaneous) and greeting; play needs the app.
const uint8_t kCompared[] = {0, 2};

struct Day {
  uint32_t shown[kInteractions][kParts][kActions];
  uint8_t greedy[kInteractions][kParts];  // learner at the end of the day
  uint32_t fingerprint;
  uint32_t decisions, pets, shoos;
};

struct Life {
  Day day[kMaxDays];
  uint32_t days;
  uint32_t bootFingerprint;
};

struct Run {
  const char* name;
  OwnerConfig owner;
  uint64_t ownerSeed;    // owner timing and reactions
  uint64_t entropySeed;  // BoardLab entropy (PetId at migration)
  uint32_t days;
  uint32_t absentFrom;   // owner stops coming from this local day
  uint32_t badDay;       // owner shoos everything on this local day
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
  r.days = kMaxDays;
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

void count(const LifeCoreStepOutput& out, Day& d) {
  uint8_t action = 0;
  if (!learnIndexFor(static_cast<ActionId>(out.selectedAction), action)) return;
  if (out.decisionContext >= kInteractions || out.decisionDayPart >= kParts) return;
  ++d.shown[out.decisionContext][out.decisionDayPart][action];
  ++d.decisions;
}

bool live(const Run& spec, Life& life) {
  std::memset(&life, 0, sizeof(life));
  life.days = spec.days;
  BoardLab lab(spec.entropySeed);
  lab.store().seedV1(xuna_fixture::fresh, sizeof(xuna_fixture::fresh));
  if (!lab.boot(0)) return false;
  life.bootFingerprint = lab.core().learner().fingerprint();
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
      if (planned != kNever) greedyInto(lab, life.day[planned]);
      planned = d;
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
      if (minute % 15 == 0 && !reactAt && !lab.core().reactionOpen(now) &&
          lab.greeting(now)) {
        const LifeCoreStepOutput& out = lab.adapter().last();
        count(out, day);
        uint8_t shown = 0;
        learnIndexFor(static_cast<ActionId>(out.selectedAction), shown);
        if (d == spec.badDay || model.rates(rng)) {
          reactAt = t + 1000 + uint64_t(rng.below(3000));
          reactPet = d != spec.badDay && model.approves(d, out.decisionContext, shown, rng);
        }
      }
    }
    // A pet is GPIO0: only inside the reaction window, or it would be a greeting.
    if (reactAt && t >= reactAt && lab.core().reactionOpen(now)) {
      if (reactPet) lab.greeting(now);
      else lab.idleButton(now);
      if (lab.adapter().lastReactionApplied()) ++(reactPet ? day.pets : day.shoos);
      reactAt = 0;
    }
    lab.loop(now);
    const LifeCoreStepOutput& out = lab.adapter().last();
    if (out.decisionShown) {
      count(out, day);
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
  greedyInto(lab, life.day[planned]);
  return true;
}

// ---- metrics (host doubles; never part of xuna_core) ----

uint32_t cell(const Life& l, uint32_t from, uint32_t to, uint8_t i, uint8_t p, double out[kActions]) {
  uint32_t n[kActions] = {};
  uint32_t total = 0;
  for (uint32_t d = from; d <= to && d < l.days; ++d)
    for (uint8_t a = 0; a < kActions; ++a) n[a] += l.day[d].shown[i][p][a];
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
                  uint32_t bt, uint32_t minSamples) {
  Distance r = {0, 0};
  for (uint8_t i : kCompared)
    for (uint8_t p = 0; p < kParts; ++p) {
      double x[kActions], y[kActions];
      if (cell(a, af, at, i, p, x) < minSamples || cell(b, bf, bt, i, p, y) < minSamples)
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

void metric(const char* scenario, const char* name, double value, const char* note = "") {
  std::printf("metric,%s,%s,%.4f,%s\n", scenario, name, value, note);
}

// Last week of a 30-day life: local days 23–29.
constexpr uint32_t kWeekFrom = 23, kWeekTo = 29;

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
      same = same && std::memcmp(&g_lives[0].day[d], &g_lives[s].day[d], sizeof(Day)) == 0;
    identical += same;
    sum += distance(g_lives[0], kWeekFrom, kWeekTo, g_lives[s], kWeekFrom, kWeekTo, 10).value;
  }
  metric("entropy_only", "lives_identical_to_seed1", identical, "of 4: same owner draws, only the PetId entropy differs");
  metric("entropy_only", "js_mean", sum / (kSeeds - 1));
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
  return true;
}

}  // namespace

int main() {
  const auto start = std::chrono::steady_clock::now();
  std::printf("# PE-0 baseline. run,<scenario>,<owner kind>,<owner seed>,<entropy seed>,"
              "<decisions>,<pets>,<shoos>,<day-29 greedy quiet p0-4 greeting p0-4>,<learner fingerprint>\n");
  std::printf("# metric,<scenario>,<name>,<value>,<note>\n");
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
  const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
  std::fprintf(stderr, "runtime_s=%.1f\n", spent.count());
  return ok ? 0 : 1;
}
