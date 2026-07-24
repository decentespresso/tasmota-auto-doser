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
