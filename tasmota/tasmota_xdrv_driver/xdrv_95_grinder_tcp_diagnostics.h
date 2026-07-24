struct {
  uint32_t network_up = 0;
  uint32_t network_down = 0;
  uint32_t local_ip_changes = 0;
  uint32_t bssid_changes = 0;
  uint32_t link_changes = 0;
  uint32_t wifi_disconnect_events = 0;
  uint32_t wifi_lost_ip_events = 0;
  uint32_t wifi_got_ip_events = 0;
  uint32_t wifi_event_generation = 0;
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
  uint32_t mdns_attempts = 0;
  uint32_t mdns_successes = 0;
  uint32_t mdns_failures = 0;
  uint32_t mdns_forced_refreshes = 0;
  uint32_t mdns_refresh_successes = 0;
  uint32_t mdns_responder_unavailable = 0;
  uint32_t mdns_service_failures = 0;
  uint32_t mdns_txt_failures = 0;
  uint32_t network_generation = 0;
  uint32_t server_generation = 0;
  uint32_t min_free_heap = UINT32_MAX;
  uint32_t observed_ip = 0;
  uint32_t active_remote_ip = 0;
  uint32_t last_event_at = 0;
  uint32_t last_wifi_event_at = 0;
  uint32_t last_disconnect_at = 0;
  uint32_t last_reconnect_duration = 0;
  uint32_t max_loop_gap = 0;
  uint32_t max_mdns_duration = 0;
  uint32_t last_loop_at = 0;
  uint32_t identity_check_at = 0;
  uint16_t observed_link_count = 0;
  uint16_t active_remote_port = 0;
  uint8_t last_disconnect_reason = 0;
  char observed_bssid[18] = { 0 };
  char last_event[24] = "boot";
  char last_wifi_event[16] = "boot";
  char last_close_reason[24] = "none";
  char last_fail_safe_reason[24] = "boot";
  char last_network_reason[24] = "boot";
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

const char kGrinderCommands[] PROGMEM = "Grinder|Status|Restart";

void CmndGrinderStatus(void);
void CmndGrinderRestart(void);

void (* const GrinderCommand[])(void) PROGMEM = {
  &CmndGrinderStatus,
  &CmndGrinderRestart
};

void CmndGrinderRestart(void) {
  GrinderTcpRestartServer("manual_restart");
  CmndGrinderStatus();
}

void CmndGrinderStatus(void) {
  const String local_ip = WiFi.localIP().toString();
  const String subnet = WiFi.subnetMask().toString();
  const String gateway = WiFi.gatewayIP().toString();
  const String remote_ip = IPAddress(GrinderTcpDiag.active_remote_ip).toString();
  const uint32_t now = millis();
  const uint32_t last_rx_age = GrinderTcp.client_open ? now - GrinderTcp.last_rx : 0;
  const uint32_t event_age = now - GrinderTcpDiag.last_event_at;
  const uint32_t wifi_event_age = now - GrinderTcpDiag.last_wifi_event_at;
  const uint32_t free_heap = ESP_getFreeHeap();
  const uint32_t min_free_heap = (UINT32_MAX == GrinderTcpDiag.min_free_heap) ? free_heap : GrinderTcpDiag.min_free_heap;
  const bool network_usable = (WL_CONNECTED == WiFi.status()) && WifiHasIPv4();
  char bssid[18];
  GrinderTcpFormatBssid(bssid, sizeof(bssid));
  Response_P(PSTR("{\"GrinderStatus\":{\"Net\":{\"Up\":%u,\"WL\":%d,\"IP\":\"%s\",\"Mask\":\"%s\",\"GW\":\"%s\",\"BSSID\":\"%s\",\"RSSI\":%d,\"Gen\":%u,\"Link\":%u,\"Reason\":%u,\"RecoveryMs\":%u},"),
             network_usable, (int)WiFi.status(), local_ip.c_str(), subnet.c_str(), gateway.c_str(), bssid, WiFi.RSSI(), GrinderTcpDiag.network_generation, GrinderTcpDiag.observed_link_count, GrinderTcpDiag.last_disconnect_reason, GrinderTcpDiag.last_reconnect_duration);
  ResponseAppend_P(PSTR("\"TCP\":{\"Listen\":%u,\"Gen\":%u,\"Client\":%u,\"Hello\":%u,\"Closing\":%u,\"PeerIP\":\"%s\",\"PeerPort\":%u,\"RxAge\":%u},"),
                   GrinderTcp.server_started, GrinderTcpDiag.server_generation, GrinderTcp.client_open, GrinderTcp.greeted, GrinderTcp.close_pending, remote_ip.c_str(), GrinderTcpDiag.active_remote_port, last_rx_age);
  ResponseAppend_P(PSTR("\"mDNS\":{\"Ad\":%u,\"Up\":%u,\"MaxMs\":%u},\"Relay\":{\"Owner\":%u,\"On\":%u},\"Heap\":{\"Free\":%u,\"Min\":%u},\"Loop\":{\"MaxGapMs\":%u},"),
                   GrinderTcp.advertised, Mdns.begun, GrinderTcpDiag.max_mdns_duration, GrinderTcpRelayOwned(), GrinderTcpRelayStateOn(), free_heap, min_free_heap, GrinderTcpDiag.max_loop_gap);
  ResponseAppend_P(PSTR("\"Last\":{\"Event\":\"%s\",\"Age\":%u,\"WiFi\":\"%s\",\"WiFiAge\":%u,\"Net\":\"%s\",\"Close\":\"%s\",\"Off\":\"%s\"},"),
                   GrinderTcpDiag.last_event, event_age, GrinderTcpDiag.last_wifi_event, wifi_event_age, GrinderTcpDiag.last_network_reason, GrinderTcpDiag.last_close_reason, GrinderTcpDiag.last_fail_safe_reason);
  ResponseAppend_P(PSTR("\"Count\":{\"NetUp\":%u,\"NetDn\":%u,\"IP\":%u,\"BSSID\":%u,\"Link\":%u,\"WiFiDn\":%u,\"LostIP\":%u,\"GotIP\":%u,\"Start\":%u,\"Stop\":%u,\"Restart\":%u,\"Accept\":%u,\"Busy\":%u,\"Close\":%u,"),
                   GrinderTcpDiag.network_up, GrinderTcpDiag.network_down, GrinderTcpDiag.local_ip_changes, GrinderTcpDiag.bssid_changes, GrinderTcpDiag.link_changes, GrinderTcpDiag.wifi_disconnect_events, GrinderTcpDiag.wifi_lost_ip_events, GrinderTcpDiag.wifi_got_ip_events, GrinderTcpDiag.server_starts, GrinderTcpDiag.server_stops, GrinderTcpDiag.forced_restarts, GrinderTcpDiag.accepted_clients, GrinderTcpDiag.busy_clients, GrinderTcpDiag.active_disconnects);
  ResponseAppend_P(PSTR("\"HelloTO\":%u,\"HeartbeatTO\":%u,\"Protocol\":%u,\"MdnsTry\":%u,\"MdnsOk\":%u,\"MdnsFail\":%u,\"MdnsRefresh\":%u,\"MdnsRefreshOk\":%u,\"MdnsDown\":%u,\"MdnsSvcFail\":%u,\"MdnsTxtFail\":%u}}}"),
                   GrinderTcpDiag.hello_timeouts, GrinderTcpDiag.heartbeat_timeouts, GrinderTcpDiag.protocol_errors, GrinderTcpDiag.mdns_attempts, GrinderTcpDiag.mdns_successes, GrinderTcpDiag.mdns_failures, GrinderTcpDiag.mdns_forced_refreshes, GrinderTcpDiag.mdns_refresh_successes, GrinderTcpDiag.mdns_responder_unavailable, GrinderTcpDiag.mdns_service_failures, GrinderTcpDiag.mdns_txt_failures);
}
