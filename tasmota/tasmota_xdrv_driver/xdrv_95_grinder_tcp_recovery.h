bool GrinderTcpNetworkUsable(void) {
  return (WL_CONNECTED == WiFi.status()) && WifiHasIPv4();
}

void GrinderTcpResetMdnsResponder(void) {
  if (Mdns.begun) {
    MDNS.end();
    Mdns.begun = 0;
  }
  GrinderTcp.advertised = false;
}

void GrinderTcpMarkNetworkDown(const char *reason) {
  if (GrinderTcpDiag.network_connected) {
    GrinderTcpDiag.network_down++;
  }
  GrinderTcpDiag.network_connected = false;
  GrinderTcpDiag.identity_check_at = 0;
  if (GrinderTcp.server_started || GrinderTcp.client_open || GrinderTcpRelayStateOn()) {
    GrinderTcpStop(reason);
  }
  GrinderTcpResetMdnsResponder();
}

void GrinderTcpEnsureMdns(void) {
  Settings->flag3.mdns_enabled = 1;
  const bool was_begun = Mdns.begun;
  if (!Mdns.begun) {
    StartMdns();
  }
  if (Mdns.begun && GrinderTcpNetworkUsable()) {
    WifiMDNSAfterReconnectv4();
  }
  if (!was_begun && Mdns.begun) {
    GrinderTcp.advertised = false;
  }
#if defined(USE_WEBSERVER) && defined(WEBSERVER_ADVERTISE)
  if ((1 == Mdns.begun) && Settings->webserver) {
    MdnsAddServiceHttp();
  }
#endif
}

bool GrinderTcpWriteMdnsTxt(char *service, char *proto, char *key_mac, char *key_name, char *key_model, char *key_proto, char *model, char *proto_version) {
  return MDNS.addServiceTxt(service, proto, key_mac, GrinderTcp.plug_mac) &&
         MDNS.addServiceTxt(service, proto, key_name, NetworkHostname()) &&
         MDNS.addServiceTxt(service, proto, key_model, model) &&
         MDNS.addServiceTxt(service, proto, key_proto, proto_version);
}

void GrinderTcpRemoveMdnsService(void) {
#ifdef ESP32
  if (Mdns.begun) {
    mdns_service_remove("_grinderplug", "_tcp");
  }
#endif
  GrinderTcp.advertised = false;
}

void GrinderTcpRecordMdnsDuration(const uint32_t started) {
  const uint32_t duration = millis() - started;
  if (duration > GrinderTcpDiag.max_mdns_duration) {
    GrinderTcpDiag.max_mdns_duration = duration;
  }
}

void GrinderTcpAdvertise(void) {
  if (!TimeReached(GrinderTcp.mdns_retry_at)) {
    return;
  }
  GrinderTcp.mdns_retry_at = millis() + GRINDER_TCP_MDNS_RETRY;
  const uint32_t started = millis();
  GrinderTcpEnsureMdns();
  if (!Mdns.begun) {
    GrinderTcpDiag.mdns_responder_unavailable++;
    GrinderTcpRecordMdnsDuration(started);
    return;
  }
  if (GrinderTcp.advertised) {
    GrinderTcpRecordMdnsDuration(started);
    return;
  }
  GrinderTcpRemoveMdnsService();
  char service[] = "grinderplug";
  char proto[] = "tcp";
  char key_mac[] = "mac";
  char key_name[] = "name";
  char key_model[] = "model";
  char key_proto[] = "proto";
  char model[] = GRINDER_TCP_MODEL;
  char proto_version[] = "1";
  const bool service_added = MDNS.addService(service, proto, GRINDER_TCP_PORT);
  const bool txt_added = GrinderTcpWriteMdnsTxt(service, proto, key_mac, key_name, key_model, key_proto, model, proto_version);
  GrinderTcpDiag.mdns_attempts++;
  if (!service_added) {
    GrinderTcpDiag.mdns_service_failures++;
  }
  if (!txt_added) {
    GrinderTcpDiag.mdns_txt_failures++;
  }
  AddLog(LOG_LEVEL_INFO,
         PSTR("GTC: mDNS service %u txt %u host %s mac %s port %u"),
         service_added,
         txt_added,
         NetworkHostname(),
         GrinderTcp.plug_mac,
         GRINDER_TCP_PORT);
  if (service_added && txt_added) {
    GrinderTcp.advertised = true;
    GrinderTcpDiag.mdns_successes++;
    GrinderTcpRecordEvent("mdns_registered");
  } else {
    GrinderTcpDiag.mdns_failures++;
    GrinderTcpRecordEvent("mdns_failed");
  }
  GrinderTcpRecordMdnsDuration(started);
}

void GrinderTcpCheckNetwork(void) {
  const uint32_t wifi_event_generation = WifiEventGeneration();
  if (wifi_event_generation != GrinderTcpDiag.observed_wifi_event_generation) {
    GrinderTcpDiag.identity_check_at = 0;
    GrinderTcpMarkNetworkDown("wifi_event_down");
  }
  if (!TimeReached(GrinderTcpDiag.identity_check_at)) {
    return;
  }
  GrinderTcpDiag.identity_check_at = millis() + GRINDER_TCP_IDENTITY_CHECK;
  if (!GrinderTcpNetworkUsable()) {
    GrinderTcpMarkNetworkDown("network_down");
    return;
  }

  const bool reconnected = !GrinderTcpDiag.network_connected;
  char bssid[18];
  GrinderTcpFormatBssid(bssid, sizeof(bssid));
  const uint32_t local_ip = (uint32_t)WiFi.localIP();
  const uint16_t link_count = WifiLinkCount();
  const uint32_t wifi_event_changes = wifi_event_generation - GrinderTcpDiag.observed_wifi_event_generation;
  const bool ip_changed = GrinderTcpDiag.identity_valid && (local_ip != GrinderTcpDiag.observed_ip);
  const bool bssid_changed = GrinderTcpDiag.identity_valid && strcmp(bssid, GrinderTcpDiag.observed_bssid);
  const bool link_changed = GrinderTcpDiag.identity_valid &&
                            ((link_count != GrinderTcpDiag.observed_link_count) ||
                             wifi_event_changes);

  GrinderTcpDiag.local_ip_changes += ip_changed;
  GrinderTcpDiag.bssid_changes += bssid_changed;
  GrinderTcpDiag.link_changes += link_changed;
  GrinderTcpDiag.observed_ip = local_ip;
  GrinderTcpDiag.observed_link_count = link_count;
  GrinderTcpDiag.observed_wifi_event_generation = wifi_event_generation;
  strlcpy(GrinderTcpDiag.observed_bssid, bssid, sizeof(GrinderTcpDiag.observed_bssid));
  GrinderTcpDiag.identity_valid = true;
  GrinderTcpDiag.network_connected = true;
  if (reconnected) {
    GrinderTcpDiag.network_up++;
  }

  if (!reconnected && !ip_changed && !bssid_changed && !link_changed) {
    if (!GrinderTcp.server_started) {
      GrinderTcpRestartServer("listener_missing");
    }
    return;
  }

  GrinderTcpDiag.network_generation += wifi_event_changes ? wifi_event_changes : 1;
  const char *reason = "network_reconnect";
  if (link_changed) {
    reason = "wifi_link_change";
  } else if (ip_changed && bssid_changed) {
    reason = "ip_bssid_change";
  } else if (ip_changed) {
    reason = "local_ip_change";
  } else if (bssid_changed) {
    reason = "bssid_change";
  }
  strlcpy(GrinderTcpDiag.last_network_reason, reason, sizeof(GrinderTcpDiag.last_network_reason));
  GrinderTcpRestartServer(reason);
}

void GrinderTcpNetworkUp(void) {
  GrinderTcpDiag.identity_check_at = 0;
  GrinderTcpCheckNetwork();
}

void GrinderTcpNetworkDown(void) {
  GrinderTcpMarkNetworkDown("network_down");
}
