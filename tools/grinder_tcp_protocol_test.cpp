#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_protocol.h"

static void ExpectAction(const char *line, const bool greeted, const GrinderTcpAction action) {
  const GrinderTcpParseResult result = GrinderTcpParseLine(line, greeted);
  assert(action == result.action);
  assert(GRINDER_TCP_REASON_NONE == result.reason);
}

static void ExpectError(const char *line, const bool greeted, const GrinderTcpReason reason) {
  const GrinderTcpParseResult result = GrinderTcpParseLine(line, greeted);
  assert(GRINDER_TCP_ACTION_NONE == result.action);
  assert(reason == result.reason);
}

static void TestMacValidation(void) {
  assert(GrinderTcpIsMac("A4:C1:38:12:34:56"));
  assert(!GrinderTcpIsMac("a4:C1:38:12:34:56"));
  assert(!GrinderTcpIsMac("A4-C1-38-12-34-56"));
  assert(!GrinderTcpIsMac("A4:C1:38:12:34"));
  assert(!GrinderTcpIsMac("A4:C1:38:12:34:5G"));
}

static void TestParser(void) {
  ExpectAction("HELLO A4:C1:38:12:34:56", false, GRINDER_TCP_ACTION_HELLO);
  ExpectAction("PING", true, GRINDER_TCP_ACTION_PING);
  ExpectAction("!", true, GRINDER_TCP_ACTION_OFF);
  ExpectAction("OFF", true, GRINDER_TCP_ACTION_OFF);
  ExpectAction("ON", true, GRINDER_TCP_ACTION_ON);
  ExpectAction("STATE", true, GRINDER_TCP_ACTION_STATE);
  ExpectAction("BYE", true, GRINDER_TCP_ACTION_BYE);
  ExpectError("PING", false, GRINDER_TCP_REASON_BEFORE_HELLO);
  ExpectError("!", false, GRINDER_TCP_REASON_BEFORE_HELLO);
  ExpectError("HELLO", false, GRINDER_TCP_REASON_BAD_HELLO);
  ExpectError("HELLO a4:C1:38:12:34:56", false, GRINDER_TCP_REASON_BAD_MAC);
  ExpectError("HELLO A4:C1:38:12:34:56", true, GRINDER_TCP_REASON_DUPLICATE_HELLO);
  ExpectError("HELLO A4:C1:38:12:34:56 extra", false, GRINDER_TCP_REASON_EXTRA_ARGS);
  ExpectError("PING extra", false, GRINDER_TCP_REASON_EXTRA_ARGS);
  ExpectError("PING extra", true, GRINDER_TCP_REASON_EXTRA_ARGS);
  ExpectError("", false, GRINDER_TCP_REASON_UNKNOWN_COMMAND);
  ExpectError("", true, GRINDER_TCP_REASON_UNKNOWN_COMMAND);
  ExpectError("BOGUS", true, GRINDER_TCP_REASON_UNKNOWN_COMMAND);
}

static void TestLineReader(void) {
  GrinderTcpLineReader reader = {};
  GrinderTcpLineReset(&reader);
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'P'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'I'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'N'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'G'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, '\r'));
  assert(GRINDER_TCP_READ_LINE == GrinderTcpLineRead(&reader, '\n'));
  assert(0 == strcmp(reader.line, "PING"));
  GrinderTcpLineReset(&reader);
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'P'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'I'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'N'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'G'));
  assert(GRINDER_TCP_READ_LINE == GrinderTcpLineRead(&reader, '\n'));
  assert(0 == strcmp(reader.line, "PING"));
  GrinderTcpLineReset(&reader);
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'P'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, '\r'));
  assert(GRINDER_TCP_READ_INVALID == GrinderTcpLineRead(&reader, 'I'));
  GrinderTcpLineReset(&reader);
  for (uint32_t i = 0; i < GRINDER_TCP_MAX_LINE_LENGTH; i++) {
    assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'A'));
  }
  assert(GRINDER_TCP_READ_OVERFLOW == GrinderTcpLineRead(&reader, 'A'));
  GrinderTcpLineReset(&reader);
  assert(GRINDER_TCP_READ_EMERGENCY_OFF == GrinderTcpLineRead(&reader, '!'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, '\r'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, '\n'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'P'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'I'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'N'));
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'G'));
  assert(GRINDER_TCP_READ_LINE == GrinderTcpLineRead(&reader, '\n'));
  assert(0 == strcmp(reader.line, "PING"));
  GrinderTcpLineReset(&reader);
  assert(GRINDER_TCP_READ_NONE == GrinderTcpLineRead(&reader, 'P'));
  assert(GRINDER_TCP_READ_INVALID == GrinderTcpLineRead(&reader, '!'));
  GrinderTcpLineReset(&reader);
  assert(GRINDER_TCP_READ_INVALID == GrinderTcpLineRead(&reader, 1));
}

static void TestFormatting(void) {
  char output[64];
  GrinderTcpFormatOk(output, sizeof(output), "A4:C1:38:12:34:56", false);
  assert(0 == strcmp(output, "OK A4:C1:38:12:34:56 state=OFF"));
  GrinderTcpFormatOk(output, sizeof(output), "A4:C1:38:12:34:56", true);
  assert(0 == strcmp(output, "OK A4:C1:38:12:34:56 state=ON"));
  GrinderTcpFormatBusy(output, sizeof(output), "A4:C1:38:12:34:56");
  assert(0 == strcmp(output, "BUSY A4:C1:38:12:34:56"));
  GrinderTcpFormatErr(output, sizeof(output), "A4:C1:38:12:34:56", GRINDER_TCP_REASON_BAD_MAC);
  assert(0 == strcmp(output, "ERR A4:C1:38:12:34:56 reason=bad_mac"));
}

int main(void) {
  TestMacValidation();
  TestParser();
  TestLineReader();
  TestFormatting();
  puts("grinder_tcp_protocol_test passed");
  return 0;
}
