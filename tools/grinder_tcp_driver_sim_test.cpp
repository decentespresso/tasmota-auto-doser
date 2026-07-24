#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <string>

#include "../tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_protocol.h"

static const char *kPlugMac = "A4:C1:38:12:34:56";
static const uint32_t kHeartbeatTimeoutMs = 2000;
static const uint32_t kHelloTimeoutMs = 1000;
static const uint32_t kCloseGraceMs = 250;
static const uint32_t kTxTimeoutMs = 250;
static const uint32_t kOldMdnsRefreshMs = 60000;

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
  bool server_started = false;
  bool power_on_delay = false;
  bool power_on_delay_state = false;
  bool power_locked = false;
  uint16_t pulse_timer = 0;
  bool rel_bistable = false;
  bool relay0_used = true;
  bool relay1_used = false;
  bool advertised = false;
  bool mdns_attempted = false;
  bool mdns_begun = true;
  bool mdns_interface_enabled = true;
  bool mdns_service_added = true;
  bool mdns_txt_added = true;
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
  uint32_t deep_sleep = 3600;
  bool wifi_no_sleep = false;
  bool wifi_rescan = true;
  uint32_t devices_present = 1;
  uint32_t closing_busy = 0;
  uint32_t direct_off_count = 0;
  uint32_t generic_off_count = 0;
  uint32_t skip_sleep = 0;
  uint32_t mqtt_disconnect_count = 0;
  uint32_t udp_disconnect_count = 0;
  uint32_t fake_power_driver_count = 0;
  uint32_t normal_power_path_count = 0;
  uint32_t mdns_service_add_count = 0;
  uint32_t mdns_txt_add_count = 0;
  uint32_t mdns_end_count = 0;
  uint32_t mdns_interface_enable_count = 0;
  bool fake_power_driver_enabled = false;
  bool tcp_power_command = false;
  bool response_write_succeeds = true;
  bool response_write_blocked = false;
  bool response_pending = false;
  uint32_t response_deadline = 0;
  uint32_t network_generation = 0;
  uint32_t restart_count = 0;
  std::string local_ip;
  std::string bssid;
  bool network_connected = false;

  bool RelayOwned(void) const {
    return connected && greeted && authorized_on && !close_pending;
  }

  bool RelayHardwareSupported(void) const {
    return (1 == devices_present) && relay0_used && !relay1_used && !rel_bistable;
  }

  void NeutralizePowerControls(void) {
    power_locked = false;
    pulse_timer = 0;
    power_on_delay = false;
    power_on_delay_state = false;
  }

  void RelayOffDirect(void) {
    NeutralizePowerControls();
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

  void KeepAwakeWhileConnected(void) {
    if (connected && greeted && !close_pending && (skip_sleep < 1)) {
      skip_sleep = 1;
    }
  }

  void ApplyQuietSettings(void) {
    deep_sleep = 0;
    wifi_no_sleep = true;
    wifi_rescan = false;
    mqtt_enabled = false;
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

  void EnsureMdns(void) {
    mdns_enabled = true;
    const bool was_begun = mdns_begun;
    if (!mdns_begun) {
      mdns_begun = true;
    }
    if (mdns_begun) {
      mdns_interface_enabled = true;
      mdns_interface_enable_count++;
    }
    if (!was_begun && mdns_begun) {
      advertised = false;
    }
  }

  void Advertise(void) {
    EnsureMdns();
    if (!mdns_begun || advertised) {
      return;
    }
    mdns_attempted = true;
    mdns_service_add_count++;
    mdns_txt_add_count++;
    if (mdns_interface_enabled && mdns_service_added && mdns_txt_added) {
      advertised = true;
    }
  }

  void Loop(void) {
    if (server_started && !RelayHardwareSupported()) {
      RelayOff();
      server_started = false;
      return;
    }
    EnforceOwnership();
    KeepAwakeWhileConnected();
    const bool authenticated = connected && greeted && !close_pending;
    if (server_started && !relay_on && !authenticated) {
      Advertise();
    }
  }

  std::string Connect(void) {
    if (!server_started && !Start()) {
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
    close_at = now + kCloseGraceMs;
  }

  bool Start(void) {
    if (!RelayHardwareSupported()) {
      RelayOff();
      server_started = false;
      return false;
    }
    ApplyQuietSettings();
    NeutralizePowerControls();
    server_started = true;
    return true;
  }

  std::string Send(const char *line) {
    if (!connected || close_pending || response_pending) {
      return "";
    }
    const GrinderTcpParseResult result = GrinderTcpParseLine(line, greeted);
    last_rx = now;
    if (GRINDER_TCP_REASON_NONE != result.reason) {
      ScheduleClose();
      return Reply(FormatErr(result.reason));
    }
    switch (result.action) {
      case GRINDER_TCP_ACTION_HELLO:
        greeted = true;
        authorized_on = false;
        return Reply(FormatOk(relay_on));
      case GRINDER_TCP_ACTION_PING:
        return Reply(FormatOk(relay_on));
      case GRINDER_TCP_ACTION_OFF:
        RelayOff();
        return Reply(FormatOk(relay_on));
      case GRINDER_TCP_ACTION_ON: {
        NeutralizePowerControls();
        authorized_on = connected && greeted && !close_pending;
        tcp_power_command = true;
        SetDevicePower(authorized_on);
        tcp_power_command = false;
        if (!relay_on) {
          authorized_on = false;
        }
        EnforceOwnership();
        return Reply(FormatOk(relay_on));
      }
      case GRINDER_TCP_ACTION_STATE:
        return Reply(FormatOk(relay_on));
      case GRINDER_TCP_ACTION_BYE:
        ScheduleClose();
        return Reply(FormatOk(relay_on));
      default:
        ScheduleClose();
        return Reply(FormatErr(GRINDER_TCP_REASON_UNKNOWN_COMMAND));
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
    response_pending = false;
    response_deadline = 0;
  }

  void Restart(void) {
    DisconnectActive();
    server_started = false;
    restart_count++;
    Start();
  }

  void NetworkUp(const char *next_ip, const char *next_bssid) {
    const bool changed = !network_connected || (local_ip != next_ip) || (bssid != next_bssid);
    network_connected = true;
    local_ip = next_ip;
    bssid = next_bssid;
    if (!changed) {
      return;
    }
    network_generation++;
    Restart();
  }

  void NetworkDown(void) {
    network_connected = false;
    DisconnectActive();
    server_started = false;
    advertised = false;
    mdns_interface_enabled = false;
    if (mdns_begun) {
      mdns_begun = false;
      mdns_end_count++;
    }
  }

  std::string Reply(const std::string &response) {
    if (response_write_blocked) {
      response_pending = true;
      response_deadline = now + kTxTimeoutMs;
      return "";
    }
    if (response_write_succeeds) {
      return response;
    }
    DisconnectActive();
    return "";
  }

  void Advance(const uint32_t elapsed) {
    now += elapsed;
  }

  void Tick(void) {
    if (response_pending && ((now - response_deadline) < 0x80000000UL)) {
      DisconnectActive();
      return;
    }
    if (connected && close_pending && ((now - close_at) < 0x80000000UL)) {
      DisconnectActive();
      return;
    }
    if (!connected || close_pending) {
      return;
    }
    const uint32_t timeout = greeted ? kHeartbeatTimeoutMs : kHelloTimeoutMs;
    if ((now - last_rx) >= timeout) {
      DisconnectActive();
      return;
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
  sim.Advance(kCloseGraceMs);
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
  sim.Advance(kHeartbeatTimeoutMs);
  sim.Tick();
  assert(!sim.connected);
  assert(!sim.relay_on);
}

static void TestHelloTimeoutTurnsOff(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  sim.Advance(kHelloTimeoutMs);
  sim.Tick();
  assert(!sim.connected);
  assert(!sim.relay_on);
}

static void TestOnRemainsUntilOff(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  for (uint32_t elapsed = 500; elapsed <= 60000; elapsed += 500) {
    sim.Advance(500);
    assert(FormatOk(true) == sim.Send("PING"));
    sim.Tick();
  }
  assert(sim.connected);
  assert(sim.relay_on);
  assert(sim.authorized_on);
  assert(FormatOk(false) == sim.Send("OFF"));
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
  assert(!multi_relay.server_started);
  assert(!multi_relay.relay_on);

  GrinderTcpDriverSim missing_relay;
  missing_relay.relay0_used = false;
  assert(!missing_relay.Start());
  assert(!missing_relay.server_started);
  assert(!missing_relay.relay_on);

  GrinderTcpDriverSim bistable_relay;
  bistable_relay.rel_bistable = true;
  assert(!bistable_relay.Start());
  assert(!bistable_relay.server_started);
  assert(!bistable_relay.relay_on);
}

static void TestPowerControlsClearedBeforeOn(void) {
  GrinderTcpDriverSim sim;
  sim.power_on_delay = true;
  sim.power_on_delay_state = true;
  sim.power_locked = true;
  sim.pulse_timer = 40;
  assert(sim.Start());
  assert(!sim.power_on_delay);
  assert(!sim.power_on_delay_state);
  assert(!sim.power_locked);
  assert(0 == sim.pulse_timer);
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  sim.power_on_delay = true;
  sim.power_on_delay_state = true;
  sim.power_locked = true;
  sim.pulse_timer = 40;
  assert(FormatOk(true) == sim.Send("ON"));
  assert(!sim.power_on_delay);
  assert(!sim.power_on_delay_state);
  assert(!sim.power_locked);
  assert(0 == sim.pulse_timer);
  assert(sim.relay_on);
}

static void TestOnResponseWriteFailureTurnsOff(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  sim.response_write_succeeds = false;
  assert("" == sim.Send("ON"));
  assert(!sim.connected);
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
}

static void TestBlockedOnResponseTurnsOffAtTxDeadline(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  sim.response_write_blocked = true;
  assert("" == sim.Send("ON"));
  assert(sim.relay_on);
  assert(sim.response_pending);
  sim.Advance(kTxTimeoutMs - 1);
  sim.Tick();
  assert(sim.relay_on);
  sim.Advance(1);
  sim.Tick();
  assert(!sim.connected);
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
}

static void TestRestartDuringOnTurnsOff(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  sim.Restart();
  assert(sim.server_started);
  assert(!sim.connected);
  assert(!sim.relay_on);
  assert(!sim.authorized_on);
}

static void TestQuietDefaultsDisableNoisyServices(void) {
  GrinderTcpDriverSim sim;
  sim.ApplyQuietSettings();
  assert(!sim.mqtt_enabled);
  assert(sim.mqtt_connected);
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
  assert(0 == sim.deep_sleep);
  assert(sim.wifi_no_sleep);
  assert(!sim.wifi_rescan);
  assert(0 == sim.mqtt_disconnect_count);
  assert(0 == sim.udp_disconnect_count);
}

static void TestQuietDefaultsAreNotRewrittenInLoop(void) {
  GrinderTcpDriverSim sim;
  sim.mqtt_enabled = true;
  sim.emulation_enabled = true;
  sim.timers_enabled = true;
  sim.rules_enabled = true;
  sim.Loop();
  assert(sim.mqtt_enabled);
  assert(sim.emulation_enabled);
  assert(sim.timers_enabled);
  assert(sim.rules_enabled);
}

#include "grinder_tcp_recovery_sim_tests.h"

int main(void) {
  TestWifiEventInterleavingCannotStrandWebserver();
  TestStandaloneSameIpEventKeepsActiveDose();
  TestWifiManagerSurvivesStationDisconnect();
  TestLostIpPreservesDisconnectDiagnostics();
  TestFirstConnectionAndBusy();
  TestHundredSequentialSessions();
  TestRepeatedBusyDoesNotDisplaceActiveClient();
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
  TestReconnectImmediatelyAfterHeartbeatTimeout();
  TestHelloTimeoutTurnsOff();
  TestOnRemainsUntilOff();
  TestDuplicateOffIsIdempotent();
  TestFastOffBeforeGenericSync();
  TestEmergencyOffAlias();
  TestActiveClientKeepsAwakeAndDefersMdns();
  TestMdnsRestartReAdvertises();
  TestMdnsAddFailureRetries();
  TestMdnsDoesNotRefreshPeriodicallyWhileIdle();
  TestByeClosesWithOff();
  TestBadCommandClosesWithOff();
  TestDuplicateHelloClosesWithOff();
  TestUnsupportedRelayLayoutRefusesStart();
  TestPowerControlsClearedBeforeOn();
  TestOnResponseWriteFailureTurnsOff();
  TestBlockedOnResponseTurnsOffAtTxDeadline();
  TestRestartDuringOnTurnsOff();
  TestNetworkGenerationRestartsOnSameIpRoam();
  TestNetworkReconnectRestartsWithUnchangedIdentity();
  TestQuietDefaultsDisableNoisyServices();
  TestQuietDefaultsAreNotRewrittenInLoop();
  TestAuthenticatedClientKeepsWifiAwakeWhileIdle();
  puts("grinder_tcp_driver_sim_test passed");
  return 0;
}
