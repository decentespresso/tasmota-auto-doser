#include <assert.h>

#include "../tasmota/tasmota_xdrv_driver/xdrv_95_grinder_ble_mailbox.h"

static GrinderBleCommand Read(GrinderBleLine &line, uint8_t id, const char *text, bool greeted = true) {
  uint8_t packet[GrinderBleValueSize + 1] = { id };
  const size_t length = strlen(text);
  assert(length < sizeof(packet));
  memcpy(packet + 1, text, length);
  return GrinderBleRead(line, packet, length + 1, greeted);
}

static void TestFragmentedHello(void) {
  GrinderBleLine line{};
  assert(Read(line, 1, "HELLO AA:BB:CC:DD:E", false).pending);
  const GrinderBleCommand command = Read(line, 1, "E:FF\n", false);
  assert(!command.pending && command.id == 1);
  assert(command.parsed.action == GRINDER_TCP_ACTION_HELLO);
  assert(Read(line, 2, "ON\n", false).parsed.reason == GRINDER_TCP_REASON_BEFORE_HELLO);
  assert(Read(line, 3, "ON\n").parsed.action == GRINDER_TCP_ACTION_ON);
}

static void TestEmergencyOffPreemptsCurrentLine(void) {
  GrinderBleLine line{};
  assert(Read(line, 9, "STA").pending);
  const GrinderBleCommand stop = Read(line, 255, "!");
  assert(!stop.pending && stop.id == 255 && stop.parsed.action == GRINDER_TCP_ACTION_OFF);
  assert(Read(line, 10, "PING\n").parsed.action == GRINDER_TCP_ACTION_PING);
  assert(Read(line, 10, "!\n").parsed.reason == GRINDER_TCP_REASON_INVALID_CHAR);
}

static void TestBadFragments(void) {
  GrinderBleLine line{};
  assert(Read(line, 1, "ON").pending);
  const GrinderBleCommand wrongId = Read(line, 2, "\n");
  assert(!wrongId.pending && wrongId.id == 2);
  assert(wrongId.parsed.reason == GRINDER_TCP_REASON_INVALID_CHAR);
  assert(Read(line, 0, "ON\n").parsed.reason == GRINDER_TCP_REASON_INVALID_CHAR);
  assert(Read(line, 3, "ON\nX").parsed.reason == GRINDER_TCP_REASON_INVALID_CHAR);
  assert(Read(line, 3, "ON\r\n").parsed.reason == GRINDER_TCP_REASON_INVALID_CHAR);
  assert(Read(line, 3, "12345678901234567890").parsed.reason == GRINDER_TCP_REASON_INVALID_CHAR);
  for (size_t i = 0; i < 5; ++i) {
    assert(Read(line, 4, "1234567890123456789").pending);
  }
  assert(Read(line, 4, "12").parsed.reason == GRINDER_TCP_REASON_LINE_OVERFLOW);
}

static void TestPeerIsolationAndHandleReuse(void) {
  GrinderBleMailbox mailbox{};
  mailbox.enabled = true;
  assert(mailbox.connect(1, 10));
  assert(mailbox.connect(2, 20));
  assert(!mailbox.connect(3, 30));
  const uint32_t generation = mailbox.owner.generation;
  mailbox.subscribe(1, true);
  mailbox.subscribe(2, true);
  const uint8_t stop[] = {1, '!'};
  mailbox.write(2, stop, sizeof(stop));
  mailbox.write(2, nullptr, 0);
  GrinderBlePacket packet{};
  assert(!mailbox.take(generation, packet));
  assert(mailbox.owns(generation));
  mailbox.disconnect(1);
  assert(!mailbox.owner.connected() && mailbox.contender.handle == 2);
  assert(mailbox.connect(1, 40));
  mailbox.subscribe(1, true);
  mailbox.write(1, stop, sizeof(stop));
  assert(!mailbox.take(generation, packet));
  assert(mailbox.take(mailbox.owner.generation, packet));
}

static void TestPriorityOffAndOwnerFault(void) {
  GrinderBleMailbox mailbox{};
  mailbox.enabled = true;
  assert(mailbox.connect(1, 10));
  const uint32_t generation = mailbox.owner.generation;
  mailbox.subscribe(1, true);
  const uint8_t on[] = {1, 'O', 'N', '\n'};
  const uint8_t off[] = {2, '!'};
  for (size_t index = 0; index < GrinderBleQueueDepth; index++) {
    mailbox.write(1, on, sizeof(on));
  }
  mailbox.write(1, off, sizeof(off));
  GrinderBlePacket packet{};
  assert(mailbox.take(generation, packet));
  assert(packet.data[0] == 2 && packet.data[1] == '!');
  assert(!mailbox.take(generation, packet));
  for (size_t index = 0; index <= GrinderBleQueueDepth; index++) {
    mailbox.write(1, on, sizeof(on));
  }
  assert(mailbox.fault == GRINDER_TCP_REASON_LINE_OVERFLOW);
  assert(mailbox.faultId == 1 && !mailbox.owns(generation));
  assert(!mailbox.take(generation, packet));
  mailbox.disconnect(1);
  assert(mailbox.connect(1, 20));
  mailbox.subscribe(1, true);
  assert(mailbox.owns(mailbox.owner.generation));
  mailbox.subscribe(1, false);
  mailbox.subscribe(1, true);
  assert(!mailbox.owns(mailbox.owner.generation));
}

int main(void) {
  TestFragmentedHello();
  TestEmergencyOffPreemptsCurrentLine();
  TestBadFragments();
  TestPeerIsolationAndHandleReuse();
  TestPriorityOffAndOwnerFault();
}
