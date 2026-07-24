struct WifiEventSnapshotSim {
  uint32_t disconnected = 0;
  uint32_t lost_ip = 0;
  uint32_t got_ip = 0;
  uint32_t outages = 0;
  uint32_t down_at = 0;
  uint32_t got_ip_at = 0;
  uint8_t reason = 0;
  bool down = false;
  bool got_ip_event = false;
  bool ip_changed = false;
};

struct WifiEventBridgeSim {
  WifiEventSnapshotSim pending;
  uint32_t now = 0;
  uint32_t generation = 0;
  uint32_t outage_started_at = 0;
  uint32_t recovery_duration = 0;
  uint32_t disconnect_events = 0;
  uint32_t lost_ip_events = 0;
  uint32_t got_ip_events = 0;
  uint8_t reason = 0;
  bool outage_active = false;
  bool callback_outage_active = false;
  bool manager_active = false;
  bool web_started = true;
  bool session_active = true;
  bool relay_on = true;

  void Disconnect(const uint8_t next_reason) {
    pending.disconnected++;
    if (!callback_outage_active) {
      pending.outages++;
      pending.down_at = now;
      pending.reason = next_reason;
      callback_outage_active = true;
    } else if (!pending.reason) {
      pending.reason = next_reason;
    }
    pending.down = true;
  }

  void LostIp(void) {
    pending.lost_ip++;
    if (!callback_outage_active) {
      pending.outages++;
      pending.down_at = now;
      pending.reason = 0;
      callback_outage_active = true;
    }
    pending.down = true;
  }

  void GotIp(const bool changed) {
    pending.got_ip++;
    pending.got_ip_at = now;
    pending.got_ip_event = true;
    pending.ip_changed |= changed;
    if (changed && !callback_outage_active) {
      pending.outages++;
    }
    callback_outage_active = false;
  }

  WifiEventSnapshotSim Take(void) {
    const WifiEventSnapshotSim snapshot = pending;
    pending = WifiEventSnapshotSim{};
    return snapshot;
  }

  void Apply(const WifiEventSnapshotSim &snapshot, const bool usable) {
    disconnect_events += snapshot.disconnected;
    lost_ip_events += snapshot.lost_ip;
    got_ip_events += snapshot.got_ip;
    if (snapshot.down || snapshot.ip_changed) {
      if (snapshot.outages) {
        outage_active = true;
        outage_started_at = snapshot.down ? snapshot.down_at : snapshot.got_ip_at;
        reason = snapshot.disconnected ? snapshot.reason : 0;
        generation += snapshot.outages;
      } else if (!reason && snapshot.disconnected) {
        reason = snapshot.reason;
      }
      if (!manager_active) {
        web_started = false;
      }
      session_active = false;
      relay_on = false;
    }
    if (snapshot.got_ip_event && outage_active && usable) {
      recovery_duration = snapshot.got_ip_at - outage_started_at;
      outage_active = false;
    }
  }

  void NetworkLifecycle(const bool usable) {
    if (usable) {
      web_started = true;
    }
  }
};

static void TestWifiEventInterleavingCannotStrandWebserver(void) {
  WifiEventBridgeSim sim;
  sim.now = 100;
  sim.Disconnect(7);
  sim.now = 200;
  sim.GotIp(false);
  const WifiEventSnapshotSim first = sim.Take();
  sim.now = 250;
  sim.Disconnect(8);
  sim.Apply(first, false);
  sim.Apply(sim.Take(), false);
  sim.NetworkLifecycle(false);
  assert(!sim.web_started);
  sim.now = 400;
  sim.GotIp(false);
  sim.Apply(sim.Take(), true);
  sim.NetworkLifecycle(true);
  assert(sim.web_started);
  assert(2 == sim.generation);
}

static void TestStandaloneSameIpEventKeepsActiveDose(void) {
  WifiEventBridgeSim sim;
  sim.now = 100;
  sim.GotIp(false);
  sim.Apply(sim.Take(), true);
  assert(sim.web_started);
  assert(sim.session_active);
  assert(sim.relay_on);
  assert(0 == sim.generation);
}

static void TestWifiManagerSurvivesStationDisconnect(void) {
  WifiEventBridgeSim sim;
  sim.manager_active = true;
  sim.Disconnect(7);
  sim.Apply(sim.Take(), false);
  assert(sim.web_started);
  assert(!sim.session_active);
  assert(!sim.relay_on);
}

static void TestLostIpPreservesDisconnectDiagnostics(void) {
  WifiEventBridgeSim sim;
  sim.now = 100;
  sim.Disconnect(42);
  sim.Apply(sim.Take(), false);
  sim.now = 500;
  sim.LostIp();
  sim.Apply(sim.Take(), false);
  sim.now = 1000;
  sim.GotIp(false);
  sim.Apply(sim.Take(), true);
  assert(42 == sim.reason);
  assert(900 == sim.recovery_duration);
  assert(1 == sim.generation);
  assert(1 == sim.lost_ip_events);
}

static void TestHundredSequentialSessions(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  for (uint32_t i = 0; i < 100; i++) {
    assert("" == sim.Connect());
    assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
    assert(FormatOk(false) == sim.Send("PING"));
    assert(FormatOk(false) == sim.Send("BYE"));
    sim.Advance(kCloseGraceMs);
    sim.Tick();
    assert(!sim.connected);
  }
}

static void TestRepeatedBusyDoesNotDisplaceActiveClient(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  for (uint32_t i = 0; i < 100; i++) {
    assert(FormatBusy() == sim.Connect());
  }
  assert(sim.connected);
  assert(sim.greeted);
  assert(100 == sim.closing_busy);
}

static void TestReconnectImmediatelyAfterHeartbeatTimeout(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  sim.Advance(kHeartbeatTimeoutMs);
  sim.Tick();
  assert(!sim.connected);
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
}

static void TestActiveClientKeepsAwakeAndDefersMdns(void) {
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
  assert(!sim.mdns_attempted);
  sim.DisconnectActive();
  sim.Loop();
  assert(sim.mdns_attempted);
}

static void TestMdnsRestartReAdvertises(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  sim.Loop();
  assert(sim.advertised);
  const uint32_t service_adds = sim.mdns_service_add_count;
  sim.mdns_begun = false;
  sim.mdns_attempted = false;
  sim.Loop();
  assert(sim.mdns_begun);
  assert(sim.mdns_attempted);
  assert(sim.advertised);
  assert(service_adds + 1 == sim.mdns_service_add_count);
}

static void TestMdnsAddFailureRetries(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  sim.mdns_service_added = false;
  sim.Loop();
  assert(!sim.advertised);
  sim.mdns_service_added = true;
  sim.mdns_attempted = false;
  sim.Loop();
  assert(sim.mdns_attempted);
  assert(sim.advertised);
}

static void TestMdnsDoesNotRefreshPeriodicallyWhileIdle(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  sim.Loop();
  const uint32_t service_adds = sim.mdns_service_add_count;
  sim.Advance(kOldMdnsRefreshMs);
  sim.Loop();
  assert(service_adds == sim.mdns_service_add_count);
  sim.Advance(kOldMdnsRefreshMs * 9);
  sim.Loop();
  assert(service_adds == sim.mdns_service_add_count);
}

static void TestNetworkGenerationRestartsOnSameIpRoam(void) {
  GrinderTcpDriverSim sim;
  sim.NetworkUp("192.168.178.30", "10:20:30:40:50:60");
  assert(1 == sim.network_generation);
  assert(1 == sim.restart_count);
  sim.NetworkUp("192.168.178.30", "10:20:30:40:50:60");
  assert(1 == sim.network_generation);
  assert(1 == sim.restart_count);
  sim.NetworkUp("192.168.178.30", "10:20:30:40:50:61");
  assert(2 == sim.network_generation);
  assert(2 == sim.restart_count);
}

static void TestNetworkReconnectRestartsWithUnchangedIdentity(void) {
  GrinderTcpDriverSim sim;
  sim.NetworkUp("192.168.178.30", "10:20:30:40:50:60");
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  assert(FormatOk(true) == sim.Send("ON"));
  sim.NetworkDown();
  assert(!sim.relay_on);
  sim.NetworkUp("192.168.178.30", "10:20:30:40:50:60");
  assert(2 == sim.network_generation);
  assert(2 == sim.restart_count);
  assert(sim.server_started);
}

static void TestAuthenticatedClientKeepsWifiAwakeWhileIdle(void) {
  GrinderTcpDriverSim sim;
  assert(sim.Start());
  assert("" == sim.Connect());
  assert(FormatOk(false) == sim.Send("HELLO 10:20:30:40:50:60"));
  sim.skip_sleep = 0;
  sim.Loop();
  assert(1 == sim.skip_sleep);
  assert(!sim.relay_on);
}
