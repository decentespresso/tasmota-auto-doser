#pragma once

#include "xdrv_95_grinder_tcp_protocol.h"

constexpr char GrinderBleServiceUuid[] = "6f735400-6d61-4d8e-bb2b-f5e23b9d7801";
constexpr char GrinderBleCommandUuid[] = "6f735401-6d61-4d8e-bb2b-f5e23b9d7801";
constexpr char GrinderBleReplyUuid[] = "6f735402-6d61-4d8e-bb2b-f5e23b9d7801";
constexpr size_t GrinderBleValueSize = 20;

struct GrinderBleLine {
  uint8_t id = 0;
  size_t length = 0;
  char text[GRINDER_TCP_MAX_LINE_LENGTH + 1]{};
};

struct GrinderBleCommand {
  uint8_t id;
  bool pending;
  GrinderTcpParseResult parsed;
};

static inline bool GrinderBleIsFastOff(const uint8_t *data, size_t length) {
  return length == 2 && data[0] && data[1] == '!';
}

static inline GrinderBleCommand GrinderBleReadFailure(GrinderBleLine &line, uint8_t id, GrinderTcpReason reason) {
  line = {};
  return {id, false, GrinderTcpErrorResult(reason)};
}

static inline GrinderBleCommand GrinderBleRead(GrinderBleLine &line, const uint8_t *data, size_t length, bool greeted) {
  const uint8_t id = length ? data[0] : 0;
  if (!id || length < 2 || length > GrinderBleValueSize) {
    return GrinderBleReadFailure(line, id, GRINDER_TCP_REASON_INVALID_CHAR);
  }
  if (GrinderBleIsFastOff(data, length)) {
    line = {};
    return {id, false, GrinderTcpParseLine("!", greeted)};
  }
  if (line.id && line.id != id) {
    return GrinderBleReadFailure(line, id, GRINDER_TCP_REASON_INVALID_CHAR);
  }
  line.id = id;
  for (size_t index = 1; index < length; index++) {
    const uint8_t value = data[index];
    if (value == '\n' && index == length - 1) {
      line.text[line.length] = 0;
      const GrinderTcpParseResult parsed = GrinderTcpParseLine(line.text, greeted);
      line = {};
      return {id, false, parsed};
    }
    if (value < ' ' || value > '~' || value == '!') {
      return GrinderBleReadFailure(line, id, GRINDER_TCP_REASON_INVALID_CHAR);
    }
    if (line.length >= GRINDER_TCP_MAX_LINE_LENGTH) {
      return GrinderBleReadFailure(line, id, GRINDER_TCP_REASON_LINE_OVERFLOW);
    }
    line.text[line.length++] = value;
  }
  return {id, true, GrinderTcpActionResult(GRINDER_TCP_ACTION_NONE)};
}
