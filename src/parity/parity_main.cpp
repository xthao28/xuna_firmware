// S21 target parity build ([env:xuna_parity]): replays the embedded Life Lab
// traces on the ESP32 with the in-RAM snapshot store, prints the same lines as
// sim/lifecore_lab.cpp plus step() timing, then halts. No BLE, display or NVS:
// nothing here touches the `xuna_life` namespace.
#include <Arduino.h>
#include <esp_timer.h>
#include <cstdio>
#include <cstring>

#include <xuna/life/LifeCoreTrace.h>

#include "parity_traces.h"

SET_LOOP_TASK_STACK_SIZE(32768)

namespace {

class TimingProbe : public xuna::LifeCoreStepProbe {
 public:
  static constexpr uint32_t kBuckets = 4096;  // 1 us buckets; above = overflow
  void reset() {
    memset(counts_, 0, sizeof(counts_));
    overflow_ = 0;
    total_ = 0;
    max_ = 0;
    sum_ = 0;
  }
  void beforeStep() override { start_ = esp_timer_get_time(); }
  void afterStep() override {
    const uint32_t us = uint32_t(esp_timer_get_time() - start_);
    if (us < kBuckets) ++counts_[us]; else ++overflow_;
    if (us > max_) max_ = us;
    sum_ += us;
    ++total_;
  }
  uint32_t percentile(uint32_t per_mille) const {
    const uint64_t rank = (uint64_t(total_) * per_mille + 999) / 1000;
    uint64_t seen = 0;
    for (uint32_t us = 0; us < kBuckets; ++us) {
      seen += counts_[us];
      if (seen >= rank && rank != 0) return us;
    }
    return max_;
  }
  void print(const char* name) const {
    static const uint32_t kEdges[] = {10, 20, 50, 100, 200, 500, 1000, 2000};
    Serial.printf("timing trace=%s steps=%u mean_us=%.2f p50_us=%u p99_us=%u "
                  "p999_us=%u max_us=%u\n", name, unsigned(total_),
                  total_ ? double(sum_) / total_ : 0.0, unsigned(percentile(500)),
                  unsigned(percentile(990)), unsigned(percentile(999)),
                  unsigned(max_));
    Serial.printf("histogram trace=%s", name);
    uint32_t from = 0;
    for (uint32_t edge : kEdges) {
      uint32_t n = 0;
      for (uint32_t us = from; us < edge; ++us) n += counts_[us];
      Serial.printf(" lt%u=%u", unsigned(edge), unsigned(n));
      from = edge;
    }
    uint32_t rest = overflow_;
    for (uint32_t us = from; us < kBuckets; ++us) rest += counts_[us];
    Serial.printf(" ge%u=%u\n", unsigned(from), unsigned(rest));
  }

 private:
  uint32_t counts_[kBuckets] = {};
  uint32_t overflow_ = 0;
  uint32_t total_ = 0;
  uint32_t max_ = 0;
  uint64_t sum_ = 0;
  int64_t start_ = 0;
};

TimingProbe probe;
xuna::LearningTraceDslCommand commands[xuna::LifeCoreTraceRunner::kMaxCommands];
xuna::LifeCoreTraceReport report;

void printHex(const uint8_t* bytes, size_t size) {
  for (size_t i = 0; i < size; ++i) Serial.printf("%02x", bytes[i]);
}

// Same output as sim/lifecore_lab.cpp so the two can be diffed line by line.
bool runTrace(const ParityTrace& trace) {
  size_t count = 0;
  char line[256];
  const char* p = trace.text;
  while (*p) {
    const char* end = strchr(p, '\n');
    const size_t len = end ? size_t(end - p) : strlen(p);
    if (len >= sizeof(line) || count >= xuna::LifeCoreTraceRunner::kMaxCommands) {
      Serial.printf("trace_error=%s\n", trace.name);
      return false;
    }
    memcpy(line, p, len);
    line[len] = 0;
    const xuna::LearningTraceParseResult parsed =
        xuna::LearningTraceDslParser::parse(line);
    if (parsed.accepted) commands[count++] = parsed.command;
    else if (parsed.error != xuna::LearningTraceParseError::Ignored) {
      Serial.printf("parse_error=%u\n", unsigned(parsed.error));
      return false;
    }
    p += len + (end ? 1 : 0);
  }

  probe.reset();
  xuna::LifeCoreTraceRunner runner;
  if (!runner.run(commands, count, report, 1, 3, &probe)) {
    Serial.println("trace_error=invalid_core_trace");
    return false;
  }
  Serial.println("processed,unsupported,rejected,choices,feedback_applied,"
                 "persist_requests,persist_failures,reboots,final_event_count,"
                 "final_generation,fingerprint,activity,visual,clock_quality,"
                 "utc_minute,sync_accepted,sync_rejected,days,expect_checked,"
                 "expect_failed,final_mono_ms");
  Serial.printf("%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu\n",
                unsigned(report.processed), unsigned(report.unsupported),
                unsigned(report.rejected), unsigned(report.choices),
                unsigned(report.feedbackApplied), unsigned(report.persistRequests),
                unsigned(report.persistFailures), unsigned(report.reboots),
                unsigned(report.finalEventCount), unsigned(report.finalGeneration),
                unsigned(report.fingerprint),
                unsigned(report.finalOutput.core.activity),
                unsigned(report.finalOutput.core.visual),
                unsigned(report.finalOutput.clockQuality),
                unsigned(report.finalOutput.utcMinute), unsigned(report.syncAccepted),
                unsigned(report.syncRejected), unsigned(report.simulatedDays),
                unsigned(report.expectChecked), unsigned(report.expectFailed),
                static_cast<unsigned long long>(report.finalMonoMs));
  Serial.print("snapshot_sha256=");
  printHex(report.snapshotSha256, sizeof(report.snapshotSha256));
  Serial.println();
  const xuna::V3Explanation& e = report.explanation;
  Serial.printf("explanation valid=%u", e.valid ? 1u : 0u);
  if (e.valid)
    Serial.printf(" interaction=%u day_part=%u shown=%u greedy=%u reason=%u margin_q16=%ld",
                  unsigned(e.interaction), unsigned(e.dayPart), unsigned(e.shownAction),
                  unsigned(e.greedyAction), unsigned(e.reason), long(e.marginQ16));
  Serial.println();
  for (uint8_t a = 0; e.valid && a < e.actionCount; ++a) {
    const xuna::V3ExplanationCell& c = e.cells[a];
    Serial.printf("explanation_cell action=%u p_q16=%lu n_eff_q16=%lu positive_q16=%lu "
                  "negative_q16=%lu rated=%lu fine=%u\n", unsigned(c.action),
                  (unsigned long)c.pQ16, (unsigned long)c.nEffQ16,
                  (unsigned long)c.positiveQ16, (unsigned long)c.negativeQ16,
                  (unsigned long)c.rated, c.fine ? 1u : 0u);
  }
  return report.expectFailed == 0;
}

void runAll(unsigned pass) {
  Serial.printf("parity pass=%u begin\n", pass);
  bool ok = true;
  for (const ParityTrace& trace : kParityTraces) {
    Serial.printf("=== trace %s\n", trace.name);
    const int64_t start = esp_timer_get_time();
    ok = runTrace(trace) && ok;
    Serial.printf("runtime trace=%s us=%lld\n", trace.name,
                  static_cast<long long>(esp_timer_get_time() - start));
    probe.print(trace.name);
  }
  Serial.printf("stack high_water_bytes=%u heap_free=%u\n",
                unsigned(uxTaskGetStackHighWaterMark(nullptr)),
                unsigned(ESP.getFreeHeap()));
  Serial.printf("parity pass=%u %s\n", pass, ok ? "PASS" : "FAIL");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(3000);  // let the USB CDC host attach
  runAll(1);
}

// Halted: re-run every 20 s only so a late serial reader still sees a report.
void loop() {
  static unsigned pass = 1;
  delay(20000);
  runAll(++pass);
}
