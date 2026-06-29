#ifndef TASMOTA_XDRV_95_GRINDER_TCP_PROTOCOL_H
#define TASMOTA_XDRV_95_GRINDER_TCP_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef GRINDER_TCP_MAX_LINE_LENGTH
#define GRINDER_TCP_MAX_LINE_LENGTH 96
#endif

enum GrinderTcpAction {
  GRINDER_TCP_ACTION_NONE,
  GRINDER_TCP_ACTION_HELLO,
  GRINDER_TCP_ACTION_PING,
  GRINDER_TCP_ACTION_OFF,
  GRINDER_TCP_ACTION_ON,
  GRINDER_TCP_ACTION_STATE,
  GRINDER_TCP_ACTION_BYE
};

enum GrinderTcpReason {
  GRINDER_TCP_REASON_NONE,
  GRINDER_TCP_REASON_BAD_HELLO,
  GRINDER_TCP_REASON_BAD_MAC,
  GRINDER_TCP_REASON_DUPLICATE_HELLO,
  GRINDER_TCP_REASON_EXTRA_ARGS,
  GRINDER_TCP_REASON_BEFORE_HELLO,
  GRINDER_TCP_REASON_UNKNOWN_COMMAND,
  GRINDER_TCP_REASON_LINE_OVERFLOW,
  GRINDER_TCP_REASON_INVALID_CHAR
};

enum GrinderTcpReadResult {
  GRINDER_TCP_READ_NONE,
  GRINDER_TCP_READ_LINE,
  GRINDER_TCP_READ_OVERFLOW,
  GRINDER_TCP_READ_INVALID
};

struct GrinderTcpLineReader {
  char line[GRINDER_TCP_MAX_LINE_LENGTH + 1];
  uint16_t length;
  bool pending_cr;
};

struct GrinderTcpParseResult {
  GrinderTcpAction action;
  GrinderTcpReason reason;
};

static inline void GrinderTcpLineReset(GrinderTcpLineReader *reader) {
  reader->length = 0;
  reader->line[0] = 0;
  reader->pending_cr = false;
}

static inline GrinderTcpReadResult GrinderTcpLineRead(GrinderTcpLineReader *reader, const uint8_t value) {
  if ('\r' == value) {
    if (reader->pending_cr) {
      GrinderTcpLineReset(reader);
      return GRINDER_TCP_READ_INVALID;
    }
    reader->pending_cr = true;
    return GRINDER_TCP_READ_NONE;
  }
  if ('\n' == value) {
    reader->pending_cr = false;
    reader->line[reader->length] = 0;
    return GRINDER_TCP_READ_LINE;
  }
  if (reader->pending_cr) {
    GrinderTcpLineReset(reader);
    return GRINDER_TCP_READ_INVALID;
  }
  if ((value < 32) || (value > 126)) {
    GrinderTcpLineReset(reader);
    return GRINDER_TCP_READ_INVALID;
  }
  if (reader->length >= GRINDER_TCP_MAX_LINE_LENGTH) {
    GrinderTcpLineReset(reader);
    return GRINDER_TCP_READ_OVERFLOW;
  }
  reader->line[reader->length++] = (char)value;
  reader->line[reader->length] = 0;
  return GRINDER_TCP_READ_NONE;
}

static inline bool GrinderTcpIsUpperHex(const char value) {
  return ((value >= '0') && (value <= '9')) || ((value >= 'A') && (value <= 'F'));
}

static inline bool GrinderTcpIsMac(const char *value) {
  if (17 != strlen(value)) {
    return false;
  }
  for (uint32_t i = 0; i < 17; i++) {
    const bool separator = (2 == i) || (5 == i) || (8 == i) || (11 == i) || (14 == i);
    if (separator) {
      if (':' != value[i]) {
        return false;
      }
    } else if (!GrinderTcpIsUpperHex(value[i])) {
      return false;
    }
  }
  return true;
}

static inline bool GrinderTcpCommandHasExtraArgs(const char *line, const char *command) {
  const size_t command_length = strlen(command);
  return (0 == strncmp(line, command, command_length)) && (' ' == line[command_length]);
}

static inline GrinderTcpParseResult GrinderTcpActionResult(const GrinderTcpAction action) {
  GrinderTcpParseResult result = { action, GRINDER_TCP_REASON_NONE };
  return result;
}

static inline GrinderTcpParseResult GrinderTcpErrorResult(const GrinderTcpReason reason) {
  GrinderTcpParseResult result = { GRINDER_TCP_ACTION_NONE, reason };
  return result;
}

static inline GrinderTcpParseResult GrinderTcpParseLine(const char *line, const bool greeted) {
  if (0 == strcmp(line, "HELLO")) {
    return GrinderTcpErrorResult(GRINDER_TCP_REASON_BAD_HELLO);
  }
  if (0 == strncmp(line, "HELLO ", 6)) {
    const char *scale_mac = line + 6;
    if (nullptr != strchr(scale_mac, ' ')) {
      return GrinderTcpErrorResult(GRINDER_TCP_REASON_EXTRA_ARGS);
    }
    if (!GrinderTcpIsMac(scale_mac)) {
      return GrinderTcpErrorResult(GRINDER_TCP_REASON_BAD_MAC);
    }
    return greeted ? GrinderTcpErrorResult(GRINDER_TCP_REASON_DUPLICATE_HELLO) : GrinderTcpActionResult(GRINDER_TCP_ACTION_HELLO);
  }
  if (0 == strcmp(line, "PING")) {
    return greeted ? GrinderTcpActionResult(GRINDER_TCP_ACTION_PING) : GrinderTcpErrorResult(GRINDER_TCP_REASON_BEFORE_HELLO);
  }
  if (0 == strcmp(line, "OFF")) {
    return greeted ? GrinderTcpActionResult(GRINDER_TCP_ACTION_OFF) : GrinderTcpErrorResult(GRINDER_TCP_REASON_BEFORE_HELLO);
  }
  if (0 == strcmp(line, "ON")) {
    return greeted ? GrinderTcpActionResult(GRINDER_TCP_ACTION_ON) : GrinderTcpErrorResult(GRINDER_TCP_REASON_BEFORE_HELLO);
  }
  if (0 == strcmp(line, "STATE")) {
    return greeted ? GrinderTcpActionResult(GRINDER_TCP_ACTION_STATE) : GrinderTcpErrorResult(GRINDER_TCP_REASON_BEFORE_HELLO);
  }
  if (0 == strcmp(line, "BYE")) {
    return greeted ? GrinderTcpActionResult(GRINDER_TCP_ACTION_BYE) : GrinderTcpErrorResult(GRINDER_TCP_REASON_BEFORE_HELLO);
  }
  if (GrinderTcpCommandHasExtraArgs(line, "PING") ||
      GrinderTcpCommandHasExtraArgs(line, "OFF") ||
      GrinderTcpCommandHasExtraArgs(line, "ON") ||
      GrinderTcpCommandHasExtraArgs(line, "STATE") ||
      GrinderTcpCommandHasExtraArgs(line, "BYE")) {
    return GrinderTcpErrorResult(GRINDER_TCP_REASON_EXTRA_ARGS);
  }
  return GrinderTcpErrorResult(GRINDER_TCP_REASON_UNKNOWN_COMMAND);
}

static inline const char *GrinderTcpReasonText(const GrinderTcpReason reason) {
  switch (reason) {
    case GRINDER_TCP_REASON_BAD_HELLO: return "bad_hello";
    case GRINDER_TCP_REASON_BAD_MAC: return "bad_mac";
    case GRINDER_TCP_REASON_DUPLICATE_HELLO: return "duplicate_hello";
    case GRINDER_TCP_REASON_EXTRA_ARGS: return "extra_args";
    case GRINDER_TCP_REASON_BEFORE_HELLO: return "before_hello";
    case GRINDER_TCP_REASON_UNKNOWN_COMMAND: return "unknown_command";
    case GRINDER_TCP_REASON_LINE_OVERFLOW: return "line_overflow";
    case GRINDER_TCP_REASON_INVALID_CHAR: return "invalid_char";
    default: return "none";
  }
}

static inline const char *GrinderTcpStateText(const bool relay_on) {
  return relay_on ? "ON" : "OFF";
}

static inline int GrinderTcpFormatOk(char *output, const size_t output_size, const char *plug_mac, const bool relay_on) {
  return snprintf(output, output_size, "OK %s state=%s", plug_mac, GrinderTcpStateText(relay_on));
}

static inline int GrinderTcpFormatBusy(char *output, const size_t output_size, const char *plug_mac) {
  return snprintf(output, output_size, "BUSY %s", plug_mac);
}

static inline int GrinderTcpFormatErr(char *output, const size_t output_size, const char *plug_mac, const GrinderTcpReason reason) {
  return snprintf(output, output_size, "ERR %s reason=%s", plug_mac, GrinderTcpReasonText(reason));
}

#endif
