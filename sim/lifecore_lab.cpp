#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>

#include <xuna/life/LifeCoreTrace.h>

namespace {

int run(std::istream& input) {
  xuna::LearningTraceDslCommand commands[
      xuna::LifeCoreTraceRunner::kMaxCommands] = {};
  std::size_t commandCount = 0;
  char line[256] = {};

  while (input.getline(line, sizeof(line))) {
    const xuna::LearningTraceParseResult parsed =
        xuna::LearningTraceDslParser::parse(line);
    if (!parsed.accepted) {
      if (parsed.error == xuna::LearningTraceParseError::Ignored) continue;
      std::cerr << "parse_error="
                << static_cast<unsigned>(parsed.error) << "\n";
      return 2;
    }
    if (commandCount >= xuna::LifeCoreTraceRunner::kMaxCommands) {
      std::cerr << "trace_error=too_many_commands\n";
      return 2;
    }
    commands[commandCount++] = parsed.command;
  }
  if (input.fail() && !input.eof()) {
    std::cerr << "trace_error=line_too_long_or_read_failure\n";
    return 2;
  }
  if (input.bad()) {
    std::cerr << "trace_error=read_failure\n";
    return 2;
  }

  xuna::LifeCoreTraceReport report;
  xuna::LifeCoreTraceRunner runner;
  if (!runner.run(commands, commandCount, report, 1, 3)) {
    std::cerr << "trace_error=invalid_core_trace\n";
    return 2;
  }

  std::cout << "processed,unsupported,rejected,choices,feedback_applied,"
               "persist_requests,persist_failures,reboots,final_event_count,"
               "final_generation,fingerprint,activity,visual,clock_quality,"
               "utc_minute,sync_accepted,sync_rejected,days,expect_checked,"
               "expect_failed,final_mono_ms\n";
  std::cout << report.processed << ',' << report.unsupported << ','
            << report.rejected << ',' << report.choices << ','
            << report.feedbackApplied << ',' << report.persistRequests << ','
            << report.persistFailures << ',' << report.reboots << ','
            << report.finalEventCount << ',' << report.finalGeneration << ','
            << report.fingerprint << ','
            << static_cast<unsigned>(report.finalOutput.core.activity) << ','
            << static_cast<unsigned>(report.finalOutput.core.visual) << ','
            << static_cast<unsigned>(report.finalOutput.clockQuality) << ','
            << report.finalOutput.utcMinute << ',' << report.syncAccepted << ','
            << report.syncRejected << ',' << report.simulatedDays << ','
            << report.expectChecked << ',' << report.expectFailed << ','
            << static_cast<unsigned long long>(report.finalMonoMs) << "\n";

  static const char kHex[] = "0123456789abcdef";
  std::cout << "snapshot_sha256=";
  for (std::size_t i = 0; i < sizeof(report.snapshotSha256); ++i)
    std::cout << kHex[report.snapshotSha256[i] >> 4]
              << kHex[report.snapshotSha256[i] & 15];
  std::cout << "\n";

  // Evidence of the last shown decision (plan §9.6); the phone writes prose.
  const xuna::V3Explanation& e = report.explanation;
  std::cout << "explanation valid=" << (e.valid ? 1 : 0);
  if (e.valid) {
    std::cout << " interaction=" << unsigned(e.interaction)
              << " day_part=" << unsigned(e.dayPart)
              << " shown=" << unsigned(e.shownAction)
              << " greedy=" << unsigned(e.greedyAction)
              << " reason=" << unsigned(e.reason)
              << " margin_q16=" << e.marginQ16;
  }
  std::cout << "\n";
  for (uint8_t a = 0; e.valid && a < e.actionCount; ++a) {
    const xuna::V3ExplanationCell& c = e.cells[a];
    std::cout << "explanation_cell action=" << unsigned(c.action)
              << " p_q16=" << c.pQ16 << " n_eff_q16=" << c.nEffQ16
              << " positive_q16=" << c.positiveQ16
              << " negative_q16=" << c.negativeQ16 << " rated=" << c.rated
              << " fine=" << (c.fine ? 1 : 0) << "\n";
  }

  if (report.expectFailed != 0) {
    std::cerr << "expect_failed=" << report.expectFailed
              << " first_command=" << report.firstFailedCommand
              << " actual=" << report.firstFailedActual << "\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    std::cerr << "usage: lifecore_lab [trace-file]\n";
    return 2;
  }
  if (argc == 1) return run(std::cin);
  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "trace_error=open_failure\n";
    return 2;
  }
  return run(input);
}