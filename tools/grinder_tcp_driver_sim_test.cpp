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
  bool server_open = false;
  bool power_on_delay = false;
  bool power_on_delay_state = false;
  bool rel_bistable = false;
  bool relay0_used = true;
  bool relay1_used = false;
  bool advertised = false;
  bool mdns_attempted = false;
  bool mqtt_enabled = true;
  bool mqtt_connected = true;
  bool mqtt_retained = true;
  bool hass_discovery = true;
  bool emulation_enabled = true;
  bool timers_enabled = true;
  bool rules_enabled = true;
  bool device_groups_enabled = true;
  bool ble_enabled = true;
  bool matter_enabled = true;
  bool wizmote_enabled = true;
  bool berry_autoexec_enabled = true;
  bool mdns_enabled = false;
  uint32_t devices_present = 1;
  uint32_t closing_busy = 0;
  uint32_t direct_off_count = 0;
  uint32_t generic_off_count = 0;
  uint32_t skip_sleep = 0;
  uint32_t mqtt_disconnect_count = 0;
  uint32_t udp_disconnect_count = 0;
  uint32_t fake_power_driver_count = 0;
  uint32_t normal_power_path_count = 0;
  bool fake_power_driver_enabled = false;
  bool tcp_power_command = false;

  bool RelayOwned(void) const {
    return connected && greeted && authorized_on && !close_pending;
  }

  bool RelayHardwareSupported(void) const {
    return (1 == devices_present) && relay0_used && !relay1_used && !rel_bistable;
  }

  void NeutralizePowerDelay(void) {
    power_on_delay = false;
    power_on_delay_state = false;
  }

  void RelayOffDirect(void) {
    NeutralizePowerDelay();
    relay_on = false;
    authorized_on = false;
    direct_off_count++;
  }

  void RelayOff(void) {
    const bool sync_state = relay_on || authorized_on;
    RelayOffDirect();
    if (sync_state) {
      generic_off_count++;
      RelayOffDirect();
    }
  }

  bool SetDevicePowerGuard(const bool rpower) {
    if (!rpower) {
      authorized_on = false;
      return false;
    }
    if (tcp_power_command && RelayOwned()) {
      return false;
    }
    RelayOffDirect();
    return true;
  }

  void SetDevicePower(const bool rpower) {
    if (SetDevicePowerGuard(rpower)) {
      return;
    }
    if (fake_power_driver_enabled && rpower) {
      fake_power_driver_count++;
      relay_on = true;
      return;
    }
    normal_power_path_count++;
    relay_on = rpower;
    if (!relay_on) {
      authorized_on = false;
    }
  }

  void EnforceOwnership(void) {
    if (relay_on && !RelayOwned()) {
      RelayOff();
    }
    if (!relay_on) {
      authorized_on = false;
    }
  }

  void KeepAwakeWhileGrinding(void) {
    if (RelayOwned() && relay_on && (skip_sleep < 1)) {
      skip_sleep = 1;
    }
  }

  void Advertise(void) {
    mdns_attempted = true;
    advertised = true;
  }

  void ApplyQuietSettings(void) {
    if (mqtt_enabled) {
      mqtt_disconnect_count++;
    }
    if (emulation_enabled) {
      udp_disconnect_count++;
    }
    mqtt_enabled = false;
    mqtt_connected = false;
    mqtt_retained = false;
    hass_discovery = false;
    emulation_enabled = false;
    timers_enabled = false;
    rules_enabled = false;
    device_groups_enabled = false;
    ble_enabled = false;
    matter_enabled = false;
    wizmote_enabled = false;
    berry_autoexec_enabled = false;
    mdns_enabled = true;
  }

  void Loop(void) {
    ApplyQuietSettings();
    if (server_open && !RelayHardwareSupported()) {
      RelayOff();
      server_open = false;
      return;
    }
    EnforceOwnership();
    KeepAwakeWhileGrinding();
    if (server_open && !relay_on) {
      Advertise();
    }
  }

  std::string Connect(void) {
    if (!server_open && !Start()) {
      return "";
    }
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

  bool Start(void) {
    if (!RelayHardwareSupported()) {
      RelayOff();
      server_open = false;
      return false;
    }
    ApplyQuietSettings();
    NeutralizePowerDelay();
    server_open = true;
    return true;
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
        NeutralizePowerDelay();
        authorized_on = connected && greeted && !close_pending;
        tcp_power_command = true;
        SetDevicePower(authorized_on);
        tcp_power_command = false;
        if (!relay_on) {
          authorized_on = false;
        }
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
    SetDevicePower(true);
  }

  void ExternalOff(void) {
    SetDevicePower(false);
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
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(sim.connected);
  assert(FormatBusy() == sim.Connect());
  assert(sim.connected);
  assert(1 == sim.closing_busy);
}

static void TestBusyClientCannotControlRelay(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
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
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  sim.ExternalOn();
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
}

static void TestExternalOffClearsAuthorization(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
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
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  sim.ExternalOn();
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
}

static void TestPreDriverGuardBlocksEarlierDriver(void) {
  GrinderTcpDriverSim sim;
  sim.fake_power_driver_enabled = true;
  sim.ExternalOn();
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
  assert(0 == sim.fake_power_driver_count);
}

static void TestAuthorizedTcpOnReachesEarlierDriver(void) {
  GrinderTcpDriverSim sim;
  sim.fake_power_driver_enabled = true;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  assert(sim.relay_on);
  assert(sim.authorized_on);
  assert(1 == sim.fake_power_driver_count);
}

static void TestDeferredByeClose(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
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
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  sim.DisconnectActive();
  assert(!sim.connected);
  assert(!sim.relay_on);
}

static void TestHeartbeatTimeoutTurnsOff(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
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
  assert(sim.Start());
  assert("" == sim.Connect());
  sim.Advance(1000);
  sim.Tick();
  assert(!sim.connected);
  assert(!sim.relay_on);
}

static void TestDuplicateOffIsIdempotent(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  assert(FormatOk(false) == sim.Send("OFF"));
  assert(FormatOk(false) == sim.Send("OFF"));
  assert(sim.connected);
  assert(!sim.relay_on);
}

static void TestFastOffBeforeGenericSync(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  const uint32_t direct_before = sim.direct_off_count;
  const uint32_t generic_before = sim.generic_off_count;
  assert(FormatOk(false) == sim.Send("OFF"));
  assert(direct_before + 2 == sim.direct_off_count);
  assert(generic_before + 1 == sim.generic_off_count);
  assert(!sim.relay_on);
  assert(FormatOk(false) == sim.Send("OFF"));
  assert(direct_before + 3 == sim.direct_off_count);
  assert(generic_before + 1 == sim.generic_off_count);
}

static void TestEmergencyOffAlias(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  assert(FormatOk(false) == sim.Send("!"));
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
}

static void TestLoopKeepsAwakeAndSkipsMdnsWhileGrinding(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  sim.mdns_attempted = false;
  sim.skip_sleep = 0;
  sim.Loop();
  assert(1 == sim.skip_sleep);
  assert(!sim.mdns_attempted);
  assert(FormatOk(false) == sim.Send("OFF"));
  sim.Loop();
  assert(sim.mdns_attempted);
}

static void TestByeClosesWithOff(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
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
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatErr(GRINDER_TCP_REASON_BEFORE_HELLO) == sim.Send("ON"));
  assert(sim.connected);
  assert(sim.close_pending);
  assert(!sim.relay_on);
}

static void TestDuplicateHelloClosesWithOff(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatErr(GRINDER_TCP_REASON_DUPLICATE_HELLO) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(sim.close_pending);
  assert(!sim.relay_on);
}

static void TestUnsupportedRelayLayoutRefusesStart(void) {
  GrinderTcpDriverSim multi_relay;
  multi_relay.devices_present = 2;
  assert(!multi_relay.Start());
  assert(!multi_relay.server_open);
  assert(!multi_relay.relay_on);

  GrinderTcpDriverSim missing_relay;
  missing_relay.relay0_used = false;
  assert(!missing_relay.Start());
  assert(!missing_relay.server_open);
  assert(!missing_relay.relay_on);

  GrinderTcpDriverSim bistable_relay;
  bistable_relay.rel_bistable = true;
  assert(!bistable_relay.Start());
  assert(!bistable_relay.server_open);
  assert(!bistable_relay.relay_on);
}

static void TestPowerDelayClearedBeforeOn(void) {
  GrinderTcpDriverSim sim;
  sim.power_on_delay = true;
  sim.power_on_delay_state = true;
  assert(sim.Start());
  assert(!sim.power_on_delay);
  assert(!sim.power_on_delay_state);
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  sim.power_on_delay = true;
  sim.power_on_delay_state = true;
  assert(FormatOk(true) == sim.Send("ON"));
  assert(!sim.power_on_delay);
  assert(!sim.power_on_delay_state);
  assert(sim.relay_on);
}

static void TestQuietDefaultsDisableNoisyServices(void) {
  GrinderTcpDriverSim sim;
  sim.ApplyQuietSettings();
  assert(!sim.mqtt_enabled);
  assert(!sim.mqtt_connected);
  assert(!sim.mqtt_retained);
  assert(!sim.hass_discovery);
  assert(!sim.emulation_enabled);
  assert(!sim.timers_enabled);
  assert(!sim.rules_enabled);
  assert(!sim.device_groups_enabled);
  assert(!sim.ble_enabled);
  assert(!sim.matter_enabled);
  assert(!sim.wizmote_enabled);
  assert(!sim.berry_autoexec_enabled);
  assert(sim.mdns_enabled);
  assert(1 == sim.mqtt_disconnect_count);
  assert(1 == sim.udp_disconnect_count);
}

static void TestQuietDefaultsAreEnforcedInLoop(void) {
  GrinderTcpDriverSim sim;
  sim.mqtt_enabled = true;
  sim.emulation_enabled = true;
  sim.timers_enabled = true;
  sim.rules_enabled = true;
  sim.Loop();
  assert(!sim.mqtt_enabled);
  assert(!sim.emulation_enabled);
  assert(!sim.timers_enabled);
  assert(!sim.rules_enabled);
  assert(sim.mdns_enabled);
}

int main(void) {
  TestFirstConnectionAndBusy();
  TestBusyClientCannotControlRelay();
  TestExternalOnBlockedBeforeHello();
  TestExternalOnBlockedAfterHello();
  TestExternalOffClearsAuthorization();
  TestExternalOnDuringTcpRunFailsOff();
  TestPreDriverGuardBlocksEarlierDriver();
  TestAuthorizedTcpOnReachesEarlierDriver();
  TestDeferredByeClose();
  TestDisconnectTurnsOff();
  TestHeartbeatTimeoutTurnsOff();
  TestHelloTimeoutTurnsOff();
  TestDuplicateOffIsIdempotent();
  TestFastOffBeforeGenericSync();
  TestEmergencyOffAlias();
  TestLoopKeepsAwakeAndSkipsMdnsWhileGrinding();
  TestByeClosesWithOff();
  TestBadCommandClosesWithOff();
  TestDuplicateHelloClosesWithOff();
  TestUnsupportedRelayLayoutRefusesStart();
  TestPowerDelayClearedBeforeOn();
  TestQuietDefaultsDisableNoisyServices();
  TestQuietDefaultsAreEnforcedInLoop();
  puts("grinder_tcp_driver_sim_test passed");
  return 0;
}
