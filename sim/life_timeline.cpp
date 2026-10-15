// Life Lab Phase 0 slice 2: long LifeCore runs with activity, needs and mood
// over time. Host-only tool; steps the real LifeCore once per second.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <xuna/life/LifeCore.h>

namespace {

using namespace xuna;

enum class Profile : uint8_t { Idle, Casual, Active };

struct Options {
  Profile profile = Profile::Casual;
  uint32_t hours = 8;
  bool series = false;
  LifeTuning tuning;
  ActivityThresholds thresholds;
};

// Owner behaviour: sessions of happy/curious requests.
// Casual: every 30 min, a 3-minute session with a request every 20 s.
// Active: every 10 min, a 5-minute session with a request every 10 s.
bool ownerRequests(Profile profile, uint32_t second, uint8_t& action) {
  uint32_t period = 0, length = 0, every = 0;
  if (profile == Profile::Casual) {
    period = 30 * 60;
    length = 3 * 60;
    every = 20;
  } else if (profile == Profile::Active) {
    period = 10 * 60;
    length = 5 * 60;
    every = 10;
  } else {
    return false;
  }
  const uint32_t inPeriod = second % period;
  if (inPeriod >= length || inPeriod % every != 0) return false;
  action = (second / every) % 3 == 0 ? 3 : 1;  // mostly happy, some curious
  return true;
}

const char* activityName(Activity a) {
  switch (a) {
    case Activity::Booting: return "booting";
    case Activity::Idle: return "idle";
    case Activity::Observing: return "observing";
    case Activity::Reacting: return "reacting";
    case Activity::Playing: return "playing";
    case Activity::Resting: return "resting";
    case Activity::Sleeping: return "sleeping";
    case Activity::EvolutionTransition: return "evolution";
    case Activity::ErrorSafe: return "error_safe";
  }
  return "?";
}

const char* moodName(MoodLabel m) {
  switch (m) {
    case MoodLabel::Calm: return "calm";
    case MoodLabel::Excited: return "excited";
    case MoodLabel::Sleepy: return "sleepy";
    case MoodLabel::Curious: return "curious";
    case MoodLabel::Overwhelmed: return "overwhelmed";
  }
  return "?";
}

bool parse(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    const bool hasValue = i + 1 < argc;
    if (std::strcmp(a, "--series") == 0) {
      o.series = true;
    } else if (std::strcmp(a, "--profile") == 0 && hasValue) {
      const char* p = argv[++i];
      if (std::strcmp(p, "idle") == 0) o.profile = Profile::Idle;
      else if (std::strcmp(p, "casual") == 0) o.profile = Profile::Casual;
      else if (std::strcmp(p, "active") == 0) o.profile = Profile::Active;
      else return false;
    } else if (std::strcmp(a, "--hours") == 0 && hasValue) {
      o.hours = static_cast<uint32_t>(std::strtoul(argv[++i], 0, 10));
    } else if (std::strcmp(a, "--decay") == 0 && hasValue) {
      o.tuning.energyDecayPerMinute =
          static_cast<uint16_t>(std::strtoul(argv[++i], 0, 10));
    } else if (std::strcmp(a, "--recovery") == 0 && hasValue) {
      o.tuning.energyRecoveryPerMinute =
          static_cast<uint16_t>(std::strtoul(argv[++i], 0, 10));
    } else if (std::strcmp(a, "--rest-exit") == 0 && hasValue) {
      o.thresholds.restExit =
          static_cast<uint16_t>(std::strtoul(argv[++i], 0, 10));
    } else if (std::strcmp(a, "--stim") == 0 && hasValue) {
      o.tuning.stimulationPerInteraction =
          static_cast<uint16_t>(std::strtoul(argv[++i], 0, 10));
    } else if (std::strcmp(a, "--cost") == 0 && hasValue) {
      o.tuning.actionCostPercent =
          static_cast<uint8_t>(std::strtoul(argv[++i], 0, 10));
    } else if (std::strcmp(a, "--wake") == 0 && hasValue) {
      o.thresholds.wakeEnergy =
          static_cast<uint16_t>(std::strtoul(argv[++i], 0, 10));
    } else if (std::strcmp(a, "--resume-rest") == 0) {
      o.thresholds.resumeInterruptedRest = true;
    } else {
      return false;
    }
  }
  return o.hours > 0 && o.hours <= 72;
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  if (!parse(argc, argv, o)) {
    std::fprintf(stderr,
                 "usage: life_timeline [--profile idle|casual|active] "
                 "[--hours 1..72] [--decay N] [--recovery N] [--series]\n"
                 "  --decay/--recovery: energy per minute, Q0.16 raw "
                 "(defaults 1024/2048)\n");
    return 2;
  }

  LifeCore core(1, 3, o.tuning, o.thresholds);
  core.begin(0, true);

  const uint32_t total = o.hours * 3600U;
  uint32_t activitySeconds[9] = {};
  uint32_t moodSeconds[5] = {};
  uint32_t requests = 0, accepted = 0, restEntries = 0;
  uint32_t firstRestMinute = 0, lowEnergySeconds = 0;
  uint32_t maxStimulation = 0;
  uint64_t energySum = 0;
  uint32_t energyMinAfterHour = 65535, energyMaxAfterHour = 0;
  Activity previous = Activity::Booting;

  if (o.series)
    std::printf("minute,activity,energy,stimulation,arousal,valence,mood\n");

  for (uint32_t second = 0; second < total; ++second) {
    const MonoMs now = second * 1000U;
    LifeCoreEvent event;
    uint8_t count = 0;
    uint8_t action = 0;
    if (ownerRequests(o.profile, second, action)) {
      event = LifeCoreEvent(LifeCoreEventKind::RequestAction, 0, 0, action);
      count = 1;
      ++requests;
    }
    const LifeCoreStepOutput out = core.step({now, &event, count});
    if (count && out.actionAccepted) ++accepted;

    ++activitySeconds[static_cast<uint8_t>(out.activity)];
    ++moodSeconds[static_cast<uint8_t>(out.mood.label)];
    if (out.activity == Activity::Resting && previous != Activity::Resting) {
      ++restEntries;
      if (firstRestMinute == 0) firstRestMinute = second / 60 + 1;
    }
    previous = out.activity;
    energySum += out.needs.energy;
    if (out.needs.energy < 24576) ++lowEnergySeconds;
    if (out.needs.stimulation > maxStimulation)
      maxStimulation = out.needs.stimulation;
    if (second >= 3600) {
      if (out.needs.energy < energyMinAfterHour)
        energyMinAfterHour = out.needs.energy;
      if (out.needs.energy > energyMaxAfterHour)
        energyMaxAfterHour = out.needs.energy;
    }
    if (o.series && second % 60 == 59)
      std::printf("%u,%s,%.3f,%.3f,%.3f,%.3f,%s\n", second / 60 + 1,
                  activityName(out.activity), out.needs.energy / 65535.0,
                  out.needs.stimulation / 65535.0, out.mood.arousal / 65535.0,
                  out.mood.valence / 32768.0, moodName(out.mood.label));
  }
  if (o.series) return 0;

  const double t = static_cast<double>(total);
  std::printf(
      "profile,hours,decay,recovery,rest_exit,stim,cost_pct,idle_pct,reacting_pct,resting_pct,"
      "sleeping_pct,other_pct,rest_entries,first_rest_min,mean_energy,"
      "energy_min_after_1h,energy_max_after_1h,low_energy_pct,requests,"
      "accepted_pct,max_stimulation,calm_pct,excited_pct,sleepy_pct,"
      "curious_pct,overwhelmed_pct\n");
  const uint32_t idle = activitySeconds[static_cast<uint8_t>(Activity::Idle)];
  const uint32_t reacting =
      activitySeconds[static_cast<uint8_t>(Activity::Reacting)];
  const uint32_t resting =
      activitySeconds[static_cast<uint8_t>(Activity::Resting)];
  const uint32_t sleeping =
      activitySeconds[static_cast<uint8_t>(Activity::Sleeping)];
  const uint32_t other = total - idle - reacting - resting - sleeping;
  const char* names[] = {"idle", "casual", "active"};
  std::printf(
      "%s,%u,%u,%u,%u,%u,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%u,%u,%.3f,%.3f,%.3f,%.1f,%u,"
      "%.1f,%.3f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
      names[static_cast<uint8_t>(o.profile)], o.hours,
      o.tuning.energyDecayPerMinute, o.tuning.energyRecoveryPerMinute,
      o.thresholds.restExit, o.tuning.stimulationPerInteraction,
      o.tuning.actionCostPercent,
      100.0 * idle / t, 100.0 * reacting / t, 100.0 * resting / t,
      100.0 * sleeping / t, 100.0 * other / t, restEntries, firstRestMinute,
      energySum / t / 65535.0, energyMinAfterHour / 65535.0,
      energyMaxAfterHour / 65535.0, 100.0 * lowEnergySeconds / t, requests,
      requests ? 100.0 * accepted / requests : 0.0, maxStimulation / 65535.0,
      100.0 * moodSeconds[0] / t, 100.0 * moodSeconds[1] / t,
      100.0 * moodSeconds[2] / t, 100.0 * moodSeconds[3] / t,
      100.0 * moodSeconds[4] / t);
  return 0;
}
