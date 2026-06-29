#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <string>

#include "../tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_protocol.h"

static const char *kPlugMac = "A4:C1:38:12:34:56";

static std::string FormatOk(const bool relay_on) {
  char output[64];
  GrinderTcpFormatOk(output, sizeof(output), kPlugMac, relay_on);
  return std::string(output);
}

static std::string FormatBusy(void) {
  char output[64];
  GrinderTcpFormatBusy(output, sizeof(output), kPlugMac);
  return std::string(output);
}

static std::string FormatErr(const GrinderTcpReason reason) {
  char output[64];
  GrinderTcpFormatErr(output, sizeof(output), kPlugMac, reason);
  return std::string(output);
}

struct GrinderTcpDriverSim {
  uint32_t now = 0;
  uint32_t last_rx = 0;
  uint32_t close_at = 0;
  bool connected = false;
  bool greeted = false;
  bool relay_on = false;
  bool authorized_on = false;
  bool close_pending = false;
  uint32_t closing_busy = 0;

  bool RelayOwned(void) const {
    return connected && greeted && authorized_on && !close_pending;
  }

  void RelayOff(void) {
    relay_on = false;
    authorized_on = false;
  }

  void EnforceOwnership(void) {
    if (relay_on && !RelayOwned()) {
      RelayOff();
    }
    if (!relay_on) {
      authorized_on = false;
    }
  }

  std::string Connect(void) {
    if (connected) {
      closing_busy++;
      return FormatBusy();
    }
    connected = true;
    greeted = false;
    authorized_on = false;
    close_pending = false;
    last_rx = now;
    return "";
  }

  void ScheduleClose(void) {
    RelayOff();
    close_pending = true;
    close_at = now + 50;
  }

  std::string Send(const char *line) {
    if (!connected || close_pending) {
      return "";
    }
    const GrinderTcpParseResult result = GrinderTcpParseLine(line, greeted);
    last_rx = now;
    if (GRINDER_TCP_REASON_NONE != result.reason) {
      ScheduleClose();
      return FormatErr(result.reason);
    }
    switch (result.action) {
      case GRINDER_TCP_ACTION_HELLO:
        greeted = true;
        authorized_on = false;
        return FormatOk(relay_on);
      case GRINDER_TCP_ACTION_PING:
        return FormatOk(relay_on);
      case GRINDER_TCP_ACTION_OFF:
        RelayOff();
        return FormatOk(relay_on);
      case GRINDER_TCP_ACTION_ON:
        authorized_on = connected && greeted && !close_pending;
        relay_on = authorized_on;
        EnforceOwnership();
        return FormatOk(relay_on);
      case GRINDER_TCP_ACTION_STATE:
        return FormatOk(relay_on);
      case GRINDER_TCP_ACTION_BYE:
        ScheduleClose();
        return FormatOk(relay_on);
      default:
        ScheduleClose();
        return FormatErr(GRINDER_TCP_REASON_UNKNOWN_COMMAND);
    }
  }

  void ExternalOn(void) {
    relay_on = true;
    RelayOff();
  }

  void ExternalOff(void) {
    RelayOff();
  }

  void DisconnectActive(void) {
    RelayOff();
    connected = false;
    greeted = false;
    close_pending = false;
  }

  void Advance(const uint32_t elapsed) {
    now += elapsed;
  }

  void Tick(void) {
    if (connected && close_pending && ((now - close_at) < 0x80000000UL)) {
      DisconnectActive();
      return;
    }
    if (!connected || close_pending) {
      return;
    }
    const uint32_t timeout = greeted ? 1500 : 1000;
    if ((now - last_rx) >= timeout) {
      DisconnectActive();
    }
  }
};

static void TestFirstConnectionAndBusy(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(sim.connected);
  assert(FormatBusy() == sim.Connect());
  assert(sim.connected);
  assert(1 == sim.closing_busy);
}

static void TestBusyClientCannotControlRelay(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatBusy() == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  assert(FormatBusy() == sim.Connect());
  assert(sim.relay_on);
}

static void TestExternalOnBlockedBeforeHello(void) {
  GrinderTcpDriverSim sim;
  sim.ExternalOn();
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
  assert("" == sim.Connect());
  sim.ExternalOn();
  assert(!sim.relay_on);
}

static void TestExternalOnBlockedAfterHello(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  sim.ExternalOn();
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
}

static void TestExternalOffClearsAuthorization(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  sim.ExternalOff();
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
  sim.ExternalOn();
  assert(!sim.relay_on);
}

static void TestExternalOnDuringTcpRunFailsOff(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  sim.ExternalOn();
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
}

static void TestDeferredByeClose(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(false) == sim.Send("BYE"));
  assert(sim.connected);
  assert(sim.close_pending);
  sim.Advance(50);
  sim.Tick();
  assert(!sim.connected);
}

static void TestDisconnectTurnsOff(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  sim.DisconnectActive();
  assert(!sim.connected);
  assert(!sim.relay_on);
}

static void TestHeartbeatTimeoutTurnsOff(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  sim.Advance(1500);
  sim.Tick();
  assert(!sim.connected);
  assert(!sim.relay_on);
}

static void TestHelloTimeoutTurnsOff(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  sim.Advance(1000);
  sim.Tick();
  assert(!sim.connected);
  assert(!sim.relay_on);
}

static void TestDuplicateOffIsIdempotent(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  assert(FormatOk(false) == sim.Send("OFF"));
  assert(FormatOk(false) == sim.Send("OFF"));
  assert(sim.connected);
  assert(!sim.relay_on);
}

static void TestByeClosesWithOff(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  assert(FormatOk(false) == sim.Send("BYE"));
  assert(sim.connected);
  assert(sim.close_pending);
  assert(!sim.relay_on);
}

static void TestBadCommandClosesWithOff(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatErr(GRINDER_TCP_REASON_BEFORE_HELLO) == sim.Send("ON"));
  assert(sim.connected);
  assert(sim.close_pending);
  assert(!sim.relay_on);
}

static void TestDuplicateHelloClosesWithOff(void) {
  GrinderTcpDriverSim sim;
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatErr(GRINDER_TCP_REASON_DUPLICATE_HELLO) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(sim.close_pending);
  assert(!sim.relay_on);
}

int main(void) {
  TestFirstConnectionAndBusy();
  TestBusyClientCannotControlRelay();
  TestExternalOnBlockedBeforeHello();
  TestExternalOnBlockedAfterHello();
  TestExternalOffClearsAuthorization();
  TestExternalOnDuringTcpRunFailsOff();
  TestDeferredByeClose();
  TestDisconnectTurnsOff();
  TestHeartbeatTimeoutTurnsOff();
  TestHelloTimeoutTurnsOff();
  TestDuplicateOffIsIdempotent();
  TestByeClosesWithOff();
  TestBadCommandClosesWithOff();
  TestDuplicateHelloClosesWithOff();
  puts("grinder_tcp_driver_sim_test passed");
  return 0;
}
