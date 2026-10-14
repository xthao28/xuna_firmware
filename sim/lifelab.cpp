#include <cstdint>
#include <fstream>
#include <iostream>

#include <xuna/learning/LearningTraceDsl.h>

namespace {

void printUsage() {
  std::cerr << "usage: lifelab [trace-file]\n";
}

int run(std::istream& input) {
  xuna::LearningTraceEvent events[xuna::LearningTraceRunner::kMaxEvents] = {};
  uint8_t eventCount = 0;
  uint32_t unsupported = 0;
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
    if (eventCount >= xuna::LearningTraceRunner::kMaxEvents) {
      std::cerr << "trace_error=too_many_events\n";
      return 2;
    }
    if (!xuna::LearningTraceDslParser::toLearningEvent(parsed.command,
                                                        events[eventCount])) {
      ++unsupported;
      continue;
    }
    ++eventCount;
  }
  if (input.fail() && !input.eof()) {
    std::cerr << "trace_error=line_too_long_or_read_failure\n";
    return 2;
  }
  if (input.bad()) {
    std::cerr << "trace_error=read_failure\n";
    return 2;
  }

  xuna::LearningTraceRunner runner;
  xuna::LearningTraceReport report;
  if (!runner.run(events, eventCount, report, 1, 3)) {
    std::cerr << "trace_error=invalid_events\n";
    return 2;
  }

  std::cout << "processed,unsupported,unknown_feedback,invalid_events,samples,"
               "correct,accuracy_q16,brier_q32,baseline_brier_q32,"
               "shadow_decisions,shadow_agreements,shadow_exploratory,"
               "feedback_accepted,feedback_rejected,fingerprint\n";
  std::cout << report.processed << ',' << unsupported << ','
            << report.unknownFeedback << ',' << report.invalidEvents << ','
            << report.evaluation.samples << ',' << report.evaluation.correct
            << ',' << report.evaluation.accuracyQ16() << ','
            << static_cast<unsigned long long>(report.evaluation.brierQ32)
            << ',' << static_cast<unsigned long long>(
                          report.evaluation.baselineBrierQ32)
            << ',' << report.shadow.decisions << ',' << report.shadow.agreements
            << ',' << report.shadow.candidateExploratory << ','
            << report.shadow.feedbackAccepted << ','
            << report.shadow.feedbackRejected << ',' << report.fingerprint
            << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    printUsage();
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
