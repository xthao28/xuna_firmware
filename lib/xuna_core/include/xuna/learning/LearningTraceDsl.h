#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/learning/LearningTrace.h>

namespace xuna {

enum class LearningTraceVerb : uint8_t {
  Invalid = 0,
  Choose,
  Feedback,
  Button,
  Command,
  Sync,
  Reboot,
  Powercut,
  Day,
  Expect,
};

enum class LearningTraceParseError : uint8_t {
  None = 0,
  Ignored,
  MissingPrefix,
  InvalidTime,
  UnknownVerb,
  MissingArgument,
  InvalidArgument,
  Overflow,
  TrailingArgument,
};

struct LearningTraceDslCommand {
  uint32_t atMs = 0;
  LearningTraceVerb verb = LearningTraceVerb::Invalid;
  uint32_t value0 = 0;
  uint32_t value1 = 0;
  int32_t signedValue = 0;
  bool flag = false;
  char text[24] = {};
  char op[3] = {};
};

struct LearningTraceParseResult {
  bool accepted = false;
  LearningTraceParseError error = LearningTraceParseError::None;
  LearningTraceDslCommand command;
};

// Bounded parser for the learning-focused subset of the future S19a DSL.
// Supported lines use: @+<ms> <verb> <args> [# comment]. No heap or libc
// tokenization is used, so the same parser remains suitable for host tests.
class LearningTraceDslParser {
 public:
  static LearningTraceParseResult parse(const char* line) {
    LearningTraceParseResult result;
    if (line == nullptr) {
      result.error = LearningTraceParseError::MissingPrefix;
      return result;
    }

    const char* cursor = skipSpaces(line);
    if (*cursor == '\0' || *cursor == '#') {
      result.error = LearningTraceParseError::Ignored;
      return result;
    }
    if (cursor[0] != '@' || cursor[1] != '+') {
      result.error = LearningTraceParseError::MissingPrefix;
      return result;
    }
    cursor += 2;
    if (!readUnsigned(cursor, 10, result.command.atMs)) {
      result.error = LearningTraceParseError::InvalidTime;
      return result;
    }

    char verb[12] = {};
    if (!readToken(cursor, verb, sizeof(verb))) {
      result.error = LearningTraceParseError::MissingArgument;
      return result;
    }
    result.command.verb = parseVerb(verb);
    if (result.command.verb == LearningTraceVerb::Invalid) {
      result.error = LearningTraceParseError::UnknownVerb;
      return result;
    }

    switch (result.command.verb) {
      case LearningTraceVerb::Choose:
        if (!readUnsignedToken(cursor, 10, result.command.value0) ||
            !readUnsignedToken(cursor, 10, result.command.value1)) {
          result.error = LearningTraceParseError::InvalidArgument;
          return result;
        }
        break;
      case LearningTraceVerb::Feedback: {
        char outcome[12] = {};
        if (!readUnsignedToken(cursor, 10, result.command.value0) ||
            !readToken(cursor, outcome, sizeof(outcome)) ||
            !readUnsignedToken(cursor, 10, result.command.value1)) {
          result.error = LearningTraceParseError::InvalidArgument;
          return result;
        }
        if (!parseBoolean(outcome, result.command.flag)) {
          result.error = LearningTraceParseError::InvalidArgument;
          return result;
        }
        break;
      }
      case LearningTraceVerb::Button: {
        char button[4] = {};
        if (!readToken(cursor, button, sizeof(button)) || button[1] != '\0' ||
            (button[0] != 'A' && button[0] != 'B')) {
          result.error = LearningTraceParseError::InvalidArgument;
          return result;
        }
        result.command.value0 = button[0];
        break;
      }
      case LearningTraceVerb::Command:
        if (!readUnsignedToken(cursor, 16, result.command.value0)) {
          result.error = LearningTraceParseError::InvalidArgument;
          return result;
        }
        break;
      case LearningTraceVerb::Sync:
        if (!readUnsignedToken(cursor, 10, result.command.value0) ||
            !readSignedToken(cursor, result.command.signedValue)) {
          result.error = LearningTraceParseError::InvalidArgument;
          return result;
        }
        break;
      case LearningTraceVerb::Day:
        if (!readUnsignedToken(cursor, 10, result.command.value0)) {
          result.error = LearningTraceParseError::InvalidArgument;
          return result;
        }
        break;
      case LearningTraceVerb::Expect: {
        if (!readToken(cursor, result.command.text,
                       sizeof(result.command.text)) ||
            !readToken(cursor, result.command.op, sizeof(result.command.op)) ||
            !readUnsignedToken(cursor, 10, result.command.value0) ||
            !isComparison(result.command.op)) {
          result.error = LearningTraceParseError::InvalidArgument;
          return result;
        }
        break;
      }
      case LearningTraceVerb::Reboot:
      case LearningTraceVerb::Powercut:
      case LearningTraceVerb::Invalid:
        break;
    }

    if (!atEnd(cursor)) {
      result.error = LearningTraceParseError::TrailingArgument;
      return result;
    }
    result.accepted = true;
    result.error = LearningTraceParseError::None;
    return result;
  }

  static bool toLearningEvent(const LearningTraceDslCommand& command,
                              LearningTraceEvent& event) {
    if (command.verb == LearningTraceVerb::Choose) {
      event = LearningTraceEvent(LearningTraceKind::Choose, command.atMs,
                                 command.value0,
                                 static_cast<uint8_t>(command.value1), false,
                                 0);
      return command.value0 != 0 && command.value1 <= 255U;
    }
    if (command.verb == LearningTraceVerb::Feedback) {
      event = LearningTraceEvent(LearningTraceKind::Feedback, command.atMs,
                                 command.value0, 0, command.flag,
                                 static_cast<uint8_t>(command.value1));
      return command.value0 != 0 && command.value1 <= 255U;
    }
    return false;
  }

 private:
  static const char* skipSpaces(const char* cursor) {
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    return cursor;
  }

  static bool readToken(const char*& cursor, char* output, size_t capacity) {
    cursor = skipSpaces(cursor);
    if (*cursor == '\0' || *cursor == '#') return false;
    size_t length = 0;
    while (cursor[length] != '\0' && cursor[length] != ' ' &&
           cursor[length] != '\t' && cursor[length] != '#') {
      if (length + 1 >= capacity) return false;
      output[length] = cursor[length];
      ++length;
    }
    output[length] = '\0';
    cursor += length;
    return length != 0;
  }

  static bool readUnsigned(const char*& cursor, uint8_t base,
                           uint32_t& output) {
    if (base == 16 && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X')) {
      cursor += 2;
    }
    const char* start = cursor;
    uint32_t value = 0;
    bool digit = false;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
           *cursor != '#') {
      const int decoded = decodeDigit(*cursor);
      if (decoded < 0 || decoded >= base) return false;
      digit = true;
      if (value > (UINT32_MAX - static_cast<uint32_t>(decoded)) / base)
        return false;
      value = value * base + static_cast<uint32_t>(decoded);
      ++cursor;
    }
    if (!digit) {
      cursor = start;
      return false;
    }
    output = value;
    return true;
  }

  static bool readUnsignedToken(const char*& cursor, uint8_t base,
                                uint32_t& output) {
    cursor = skipSpaces(cursor);
    return readUnsigned(cursor, base, output);
  }

  static bool readSignedToken(const char*& cursor, int32_t& output) {
    cursor = skipSpaces(cursor);
    bool negative = false;
    if (*cursor == '+' || *cursor == '-') {
      negative = *cursor == '-';
      ++cursor;
    }
    uint32_t magnitude = 0;
    if (!readUnsigned(cursor, 10, magnitude) ||
        magnitude > (negative ? 2147483648U : 2147483647U)) {
      return false;
    }
    output = negative ? -static_cast<int32_t>(magnitude)
                      : static_cast<int32_t>(magnitude);
    return true;
  }

  static bool parseBoolean(const char* token, bool& value) {
    if (equals(token, "1") || equals(token, "true") ||
        equals(token, "positive") || equals(token, "approve") ||
        equals(token, "approved")) {
      value = true;
      return true;
    }
    if (equals(token, "0") || equals(token, "false") ||
        equals(token, "negative") || equals(token, "reject") ||
        equals(token, "rejected")) {
      value = false;
      return true;
    }
    return false;
  }

  static LearningTraceVerb parseVerb(const char* token) {
    if (equals(token, "choose")) return LearningTraceVerb::Choose;
    if (equals(token, "feedback")) return LearningTraceVerb::Feedback;
    if (equals(token, "button")) return LearningTraceVerb::Button;
    if (equals(token, "cmd")) return LearningTraceVerb::Command;
    if (equals(token, "sync")) return LearningTraceVerb::Sync;
    if (equals(token, "reboot")) return LearningTraceVerb::Reboot;
    if (equals(token, "powercut")) return LearningTraceVerb::Powercut;
    if (equals(token, "day")) return LearningTraceVerb::Day;
    if (equals(token, "expect")) return LearningTraceVerb::Expect;
    return LearningTraceVerb::Invalid;
  }

  static bool isComparison(const char* token) {
    return equals(token, "==") || equals(token, "!=") ||
           equals(token, ">=") || equals(token, "<=");
  }

  static bool atEnd(const char* cursor) {
    cursor = skipSpaces(cursor);
    return *cursor == '\0' || *cursor == '#';
  }

  static bool equals(const char* first, const char* second) {
    size_t index = 0;
    while (first[index] != '\0' || second[index] != '\0') {
      if (first[index] != second[index]) return false;
      ++index;
    }
    return true;
  }

  static int decodeDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  }
};

}  // namespace xuna
