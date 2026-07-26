struct {
  uint32_t network_up = 0;
  uint32_t network_down = 0;
  uint32_t local_ip_changes = 0;
  uint32_t bssid_changes = 0;
  uint32_t link_changes = 0;
  uint32_t observed_wifi_event_generation = 0;
  uint32_t server_starts = 0;
  uint32_t server_stops = 0;
  uint32_t forced_restarts = 0;
  uint32_t accepted_clients = 0;
  uint32_t busy_clients = 0;
  uint32_t active_disconnects = 0;
  uint32_t hello_timeouts = 0;
  uint32_t heartbeat_timeouts = 0;
  uint32_t protocol_errors = 0;
  uint32_t peer_recovery_attempts = 0;
  uint32_t peer_recovery_successes = 0;
  uint32_t peer_recovery_cancelled = 0;
  uint32_t peer_recovery_no_peer = 0;
  uint32_t peer_recovery_wifi_failures = 0;
  uint32_t peer_recovery_suppressed = 0;
  uint32_t mdns_attempts = 0;
  uint32_t mdns_successes = 0;
  uint32_t mdns_failures = 0;
  uint32_t mdns_responder_unavailable = 0;
  uint32_t mdns_service_failures = 0;
  uint32_t mdns_txt_failures = 0;
  uint32_t network_generation = 0;
  uint32_t server_generation = 0;
  uint32_t min_free_heap = UINT32_MAX;
  uint32_t observed_ip = 0;
  uint32_t active_remote_ip = 0;
  uint32_t last_event_at = 0;
  uint32_t max_loop_gap = 0;
  uint32_t max_mdns_duration = 0;
  uint32_t last_loop_at = 0;
  uint32_t identity_check_at = 0;
  uint16_t observed_link_count = 0;
  uint16_t active_remote_port = 0;
  char observed_bssid[18] = { 0 };
  char last_event[24] = "boot";
  char last_close_reason[24] = "none";
  char last_fail_safe_reason[24] = "boot";
  char last_network_reason[24] = "boot";
  char last_peer_recovery_trigger[24] = "boot";
  char last_peer_recovery_outcome[24] = "none";
  bool identity_valid = false;
  bool network_connected = false;
} GrinderTcpDiag;

bool GrinderTcpRelayOwned(void);
bool GrinderTcpRelayStateOn(void);

void GrinderTcpRecordEvent(const char *event) {
  strlcpy(GrinderTcpDiag.last_event, event, sizeof(GrinderTcpDiag.last_event));
  GrinderTcpDiag.last_event_at = millis();
}

void GrinderTcpRecordFailSafe(const char *reason) {
  strlcpy(GrinderTcpDiag.last_fail_safe_reason, reason, sizeof(GrinderTcpDiag.last_fail_safe_reason));
  GrinderTcpRecordEvent(reason);
}

void GrinderTcpFormatBssid(char *output, const size_t output_size) {
  const uint8_t *bssid = WiFi.BSSID();
  if (!bssid) {
    strlcpy(output, "00:00:00:00:00:00", output_size);
    return;
  }
  snprintf_P(output, output_size, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

void GrinderTcpObserveDiagnostics(void) {
  const uint32_t now = millis();
  if (GrinderTcpDiag.last_loop_at) {
    const uint32_t loop_gap = now - GrinderTcpDiag.last_loop_at;
    if (loop_gap > GrinderTcpDiag.max_loop_gap) {
      GrinderTcpDiag.max_loop_gap = loop_gap;
    }
  }
  GrinderTcpDiag.last_loop_at = now;
  const uint32_t free_heap = ESP_getFreeHeap();
  if (free_heap < GrinderTcpDiag.min_free_heap) {
    GrinderTcpDiag.min_free_heap = free_heap;
  }
}

const char kGrinderCommands[] PROGMEM = "Grinder|Status|Diag|Restart";

void CmndGrinderStatus(void);
void CmndGrinderDiag(void);
void CmndGrinderRestart(void);

void (* const GrinderCommand[])(void) PROGMEM = {
  &CmndGrinderStatus,
  &CmndGrinderDiag,
  &CmndGrinderRestart
};

void CmndGrinderRestart(void) {
  GrinderTcpRestartServer("manual_restart");
  CmndGrinderStatus();
}

void CmndGrinderStatus(void) {
  const String local_ip = WiFi.localIP().toString();
  const String remote_ip = IPAddress(GrinderTcpDiag.active_remote_ip).toString();
  const uint32_t now = millis();
  const uint32_t last_rx_age = GrinderTcp.client_open ? now - GrinderTcp.last_rx : 0;
  const uint32_t event_age = now - GrinderTcpDiag.last_event_at;
  const uint32_t free_heap = ESP_getFreeHeap();
  const uint32_t min_free_heap = (UINT32_MAX == GrinderTcpDiag.min_free_heap) ? free_heap : GrinderTcpDiag.min_free_heap;
  const uint32_t peer_recovery_due = GrinderTcpPeerRecoveryDueMs();
  const uint32_t peer_recovery_cooldown = GrinderTcpPeerRecoveryCooldownMs();
  const bool network_usable = (WL_CONNECTED == WiFi.status()) && WifiHasIPv4();
  char bssid[18];
  GrinderTcpFormatBssid(bssid, sizeof(bssid));
  Response_P(PSTR("{\"GrinderStatus\":{\"Net\":{\"Up\":%u,\"WL\":%d,\"IP\":\"%s\",\"BSSID\":\"%s\",\"RSSI\":%d,\"Gen\":%u,\"Link\":%u,\"Reason\":%u,\"RecoveryMs\":%u},"),
             network_usable, (int)WiFi.status(), local_ip.c_str(), bssid, WiFi.RSSI(), GrinderTcpDiag.network_generation, GrinderTcpDiag.observed_link_count, WifiLastDisconnectReason(), WifiLastRecoveryDuration());
  ResponseAppend_P(PSTR("\"TCP\":{\"Listen\":%u,\"Gen\":%u,\"Client\":%u,\"Hello\":%u,\"Closing\":%u,\"PeerIP\":\"%s\",\"PeerPort\":%u,\"RxAge\":%u},"),
                   GrinderTcp.server_started, GrinderTcpDiag.server_generation, GrinderTcp.client_open, GrinderTcp.greeted, GrinderTcp.close_pending, remote_ip.c_str(), GrinderTcpDiag.active_remote_port, last_rx_age);
  ResponseAppend_P(PSTR("\"PeerRecovery\":{\"State\":%u,\"Attempted\":%u,\"DueMs\":%u,\"CooldownMs\":%u,\"Try\":%u,\"Ok\":%u,\"Cancel\":%u,\"NoPeer\":%u,\"WiFiFail\":%u,\"Skip\":%u,\"Trigger\":\"%s\",\"Outcome\":\"%s\"},"),
                   (uint32_t)GrinderTcpPeerRecovery.state, GrinderTcpPeerRecovery.attempted, peer_recovery_due, peer_recovery_cooldown,
                   GrinderTcpDiag.peer_recovery_attempts, GrinderTcpDiag.peer_recovery_successes, GrinderTcpDiag.peer_recovery_cancelled,
                   GrinderTcpDiag.peer_recovery_no_peer, GrinderTcpDiag.peer_recovery_wifi_failures, GrinderTcpDiag.peer_recovery_suppressed,
                   GrinderTcpDiag.last_peer_recovery_trigger, GrinderTcpDiag.last_peer_recovery_outcome);
  ResponseAppend_P(PSTR("\"mDNS\":{\"Ad\":%u,\"Up\":%u,\"MaxMs\":%u},\"Relay\":{\"Owner\":%u,\"On\":%u},\"Heap\":{\"Free\":%u,\"Min\":%u},\"Loop\":{\"MaxGapMs\":%u},"),
                   GrinderTcp.advertised, Mdns.begun, GrinderTcpDiag.max_mdns_duration, GrinderTcpRelayOwned(), GrinderTcpRelayStateOn(), free_heap, min_free_heap, GrinderTcpDiag.max_loop_gap);
  ResponseAppend_P(PSTR("\"Last\":{\"Event\":\"%s\",\"Age\":%u,\"Net\":\"%s\"},"),
                   GrinderTcpDiag.last_event, event_age, GrinderTcpDiag.last_network_reason);
  ResponseAppend_P(PSTR("\"Count\":{\"NetUp\":%u,\"NetDn\":%u,\"Link\":%u,\"WiFiDn\":%u,\"LostIP\":%u,\"GotIP\":%u,\"Restart\":%u,\"MdnsFail\":%u}}}"),
                   GrinderTcpDiag.network_up, GrinderTcpDiag.network_down, GrinderTcpDiag.link_changes, WifiDisconnectEventCount(), WifiLostIpEventCount(), WifiGotIpEventCount(), GrinderTcpDiag.forced_restarts, GrinderTcpDiag.mdns_failures);
}

void CmndGrinderDiag(void) {
  Response_P(PSTR("{\"GrinderDiag\":{\"Change\":{\"IP\":%u,\"BSSID\":%u},"),
             GrinderTcpDiag.local_ip_changes, GrinderTcpDiag.bssid_changes);
  ResponseAppend_P(PSTR("\"TCP\":{\"Start\":%u,\"Stop\":%u,\"Accept\":%u,\"Busy\":%u,\"Close\":%u,\"HelloTO\":%u,\"HeartbeatTO\":%u,\"Protocol\":%u},"),
                   GrinderTcpDiag.server_starts, GrinderTcpDiag.server_stops, GrinderTcpDiag.accepted_clients, GrinderTcpDiag.busy_clients, GrinderTcpDiag.active_disconnects, GrinderTcpDiag.hello_timeouts, GrinderTcpDiag.heartbeat_timeouts, GrinderTcpDiag.protocol_errors);
  ResponseAppend_P(PSTR("\"mDNS\":{\"Try\":%u,\"Ok\":%u,\"Fail\":%u,\"Down\":%u,\"SvcFail\":%u,\"TxtFail\":%u},"),
                   GrinderTcpDiag.mdns_attempts, GrinderTcpDiag.mdns_successes, GrinderTcpDiag.mdns_failures, GrinderTcpDiag.mdns_responder_unavailable, GrinderTcpDiag.mdns_service_failures, GrinderTcpDiag.mdns_txt_failures);
  ResponseAppend_P(PSTR("\"Last\":{\"Close\":\"%s\",\"Off\":\"%s\"}}}"),
                   GrinderTcpDiag.last_close_reason, GrinderTcpDiag.last_fail_safe_reason);
}
