#ifdef USE_GRINDER_TCP

#ifndef USE_DISCOVERY
#error USE_GRINDER_TCP requires USE_DISCOVERY
#endif

#ifdef XDRV_95
#error XDRV_95 already defined
#endif

#define XDRV_95 95

#ifdef ESP32
#include "esp_mac.h"
#endif

#include "tasmota_xdrv_driver/xdrv_95_grinder_tcp_protocol.h"

#ifndef GRINDER_TCP_PORT
#define GRINDER_TCP_PORT 31980
#endif

#ifndef GRINDER_TCP_HEARTBEAT_TIMEOUT
#ifndef GRINDER_TCP_HEARTBEAT_INTERVAL
#define GRINDER_TCP_HEARTBEAT_INTERVAL 500
#endif
#ifndef GRINDER_TCP_HEARTBEAT_MISSES
#define GRINDER_TCP_HEARTBEAT_MISSES 4
#endif
#define GRINDER_TCP_HEARTBEAT_TIMEOUT (GRINDER_TCP_HEARTBEAT_INTERVAL * GRINDER_TCP_HEARTBEAT_MISSES)
#endif

#ifndef GRINDER_TCP_HELLO_TIMEOUT
#define GRINDER_TCP_HELLO_TIMEOUT 1000
#endif

#ifndef GRINDER_TCP_CLOSE_GRACE
#define GRINDER_TCP_CLOSE_GRACE 250
#endif

#ifndef GRINDER_TCP_MAX_ON_MS
#define GRINDER_TCP_MAX_ON_MS 30000
#endif

#ifndef GRINDER_TCP_ACCEPT_LIMIT
#define GRINDER_TCP_ACCEPT_LIMIT 4
#endif

#ifndef GRINDER_TCP_BUSY_CLOSE_SLOTS
#define GRINDER_TCP_BUSY_CLOSE_SLOTS 4
#endif

#ifndef GRINDER_TCP_MDNS_RETRY
#define GRINDER_TCP_MDNS_RETRY 5000
#endif

#ifndef GRINDER_TCP_MAX_BYTES_PER_LOOP
#define GRINDER_TCP_MAX_BYTES_PER_LOOP 256
#endif

#ifndef GRINDER_TCP_MODEL
#define GRINDER_TCP_MODEL "NOUS_A6T"
#endif

struct GrinderTcpClosingClient {
  WiFiClient client;
  uint32_t close_at = 0;
  bool open = false;
};

WiFiServer GrinderTcpServer(GRINDER_TCP_PORT);

struct {
  WiFiClient client;
  GrinderTcpClosingClient closing[GRINDER_TCP_BUSY_CLOSE_SLOTS];
  GrinderTcpLineReader reader;
  uint32_t last_rx = 0;
  uint32_t close_at = 0;
  uint32_t on_since = 0;
  uint32_t mdns_retry_at = 0;
  char plug_mac[18] = { 0 };
  bool server_open = false;
  bool client_open = false;
  bool greeted = false;
  bool advertised = false;
  bool authorized_on = false;
  bool close_pending = false;
  bool tcp_power_command = false;
} GrinderTcp;

void GrinderTcpAdvertise(void);
void GrinderTcpStop(void);

void GrinderTcpApplyQuietSettings(void) {
  Settings->flag.mqtt_add_global_info = 0;
  Settings->flag.mqtt_enabled = 0;
  Settings->flag.mqtt_response = 0;
  Settings->flag.mqtt_power_retain = 0;
  Settings->flag.mqtt_button_retain = 0;
  Settings->flag.mqtt_switch_retain = 0;
  Settings->flag.mqtt_sensor_retain = 0;
  Settings->flag.mqtt_offline = 0;
  Settings->flag.hass_discovery = 0;
  Settings->flag.hass_light = 0;
  Settings->flag.mqtt_serial = 0;
  Settings->flag.mqtt_serial_raw = 0;
  Settings->flag3.timers_enable = 0;
  Settings->flag3.mdns_enabled = 1;
  Settings->flag3.hass_tele_on_power = 0;
  Settings->flag3.mqtt_buttons = 0;
  Settings->flag3.no_hold_retain = 1;
  Settings->flag3.tuya_serial_mqtt_publish = 0;
  Settings->flag3.grouptopic_mode = 0;
  Settings->flag4.awsiot_shadow = 0;
  Settings->flag4.device_groups_enabled = 0;
  Settings->flag4.multiple_device_groups = 0;
  Settings->flag4.zigbee_distinct_topics = 0;
  Settings->flag4.mqtt_tls = 0;
  Settings->flag4.mqtt_no_retain = 1;
  Settings->flag5.mqtt_switches = 0;
  Settings->flag5.mi32_enable = 0;
  Settings->flag5.mqtt_state_retain = 0;
  Settings->flag5.mqtt_info_retain = 0;
  Settings->flag5.mqtt_status_retain = 0;
  Settings->flag6.mqtt_disable_publish = 1;
  Settings->flag6.mqtt_disable_modbus = 1;
  Settings->flag6.matter_enabled = 0;
  Settings->flag6.berry_no_autoexec = 1;
  Settings->flag6.wizmote_enabled = 0;
  Settings->flag2.emulation = EMUL_NONE;
  Settings->rule_enabled = 0;
  Settings->rule_once = 0;
}

void GrinderTcpNeutralizePowerDelay(void) {
  Settings->param[P_POWER_ON_DELAY2] = 0;
  TasmotaGlobal.power_on_delay = 0;
  TasmotaGlobal.power_on_delay_state = 0;
}

void GrinderTcpFormatMac(char *output, const size_t output_size) {
#ifdef ESP32
  uint8_t mac[6] = { 0, 0, 0, 0, 0, 0 };
#ifdef CONFIG_SOC_HAS_WIFI
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
#elif CONFIG_ESP_WIFI_REMOTE_ENABLED
  WiFi.macAddress(mac);
#else
  esp_read_mac(mac, ESP_MAC_BASE);
#endif
  snprintf_P(output, output_size, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#else
  strlcpy(output, NetworkMacAddress().c_str(), output_size);
#endif
}

bool GrinderTcpCacheIdentity(void) {
  if (!GrinderTcp.plug_mac[0]) {
    GrinderTcpFormatMac(GrinderTcp.plug_mac, sizeof(GrinderTcp.plug_mac));
  }
  return GrinderTcpIsMac(GrinderTcp.plug_mac);
}

bool GrinderTcpRelayStateOn(void) {
  return (TasmotaGlobal.power & 1);
}

bool GrinderTcpRelayOwned(void) {
  return GrinderTcp.client_open && GrinderTcp.greeted && GrinderTcp.authorized_on && !GrinderTcp.close_pending;
}

bool GrinderTcpRelayHardwareSupported(void) {
  return (1 == TasmotaGlobal.devices_present) &&
         PinUsed(GPIO_REL1, 0) &&
         !PinUsed(GPIO_REL1, 1) &&
         !TasmotaGlobal.rel_bistable;
}

void GrinderTcpRelayOffDirect(void) {
  GrinderTcp.authorized_on = false;
  GrinderTcp.on_since = 0;
  GrinderTcpNeutralizePowerDelay();
  TasmotaGlobal.power &= (POWER_MASK ^ 1);
  TasmotaGlobal.last_power &= (POWER_MASK ^ 1);
  TasmotaGlobal.blink_mask &= (POWER_MASK ^ 1);
  if (PinUsed(GPIO_REL1, 0)) {
    DigitalWrite(GPIO_REL1, 0, bitRead(TasmotaGlobal.rel_inverted, 0) ? 1 : 0);
  }
}

void GrinderTcpRelayOff(void) {
  const bool sync_state = GrinderTcpRelayStateOn() || GrinderTcp.authorized_on || (TasmotaGlobal.blink_mask & 1);
  GrinderTcpRelayOffDirect();
  if (!sync_state) {
    return;
  }
  ExecuteCommandPower(1, POWER_OFF_FORCE, SRC_IGNORE);
  GrinderTcpRelayOffDirect();
}

void GrinderTcpRelayOnCommand(void) {
  if (!GrinderTcp.client_open || !GrinderTcp.greeted || GrinderTcp.close_pending) {
    GrinderTcpRelayOff();
    return;
  }
  const bool was_on = GrinderTcpRelayStateOn();
  GrinderTcpNeutralizePowerDelay();
  GrinderTcp.authorized_on = true;
  GrinderTcp.tcp_power_command = true;
  ExecuteCommandPower(1, POWER_ON, SRC_IGNORE);
  GrinderTcp.tcp_power_command = false;
  if (!GrinderTcpRelayStateOn()) {
    GrinderTcp.authorized_on = false;
    GrinderTcp.on_since = 0;
    return;
  }
  if (!was_on || !GrinderTcp.on_since) {
    GrinderTcp.on_since = millis();
  }
}

void GrinderTcpWriteLine(WiFiClient &client, const char *line) {
  client.print(line);
  client.print('\n');
}

void GrinderTcpWriteOk(WiFiClient &client) {
  char response[48];
  GrinderTcpFormatOk(response, sizeof(response), GrinderTcp.plug_mac, GrinderTcpRelayStateOn());
  GrinderTcpWriteLine(client, response);
}

void GrinderTcpWriteBusy(WiFiClient &client) {
  char response[32];
  GrinderTcpFormatBusy(response, sizeof(response), GrinderTcp.plug_mac);
  GrinderTcpWriteLine(client, response);
}

void GrinderTcpWriteErr(WiFiClient &client, const uint32_t reason) {
  char response[64];
  GrinderTcpFormatErr(response, sizeof(response), GrinderTcp.plug_mac, (GrinderTcpReason)reason);
  GrinderTcpWriteLine(client, response);
}

void GrinderTcpResetActiveClient(void) {
  GrinderTcp.client.stop();
  GrinderTcp.client_open = false;
  GrinderTcp.greeted = false;
  GrinderTcp.authorized_on = false;
  GrinderTcp.close_pending = false;
  GrinderTcp.last_rx = 0;
  GrinderTcp.close_at = 0;
  GrinderTcpLineReset(&GrinderTcp.reader);
}

void GrinderTcpCloseActiveNow(const bool relay_off) {
  if (relay_off) {
    GrinderTcpRelayOff();
  }
  if (GrinderTcp.client_open) {
    GrinderTcpResetActiveClient();
  }
}

void GrinderTcpScheduleActiveClose(const bool relay_off) {
  if (relay_off) {
    GrinderTcpRelayOff();
  }
  if (!GrinderTcp.client_open) {
    GrinderTcpResetActiveClient();
    return;
  }
  GrinderTcp.authorized_on = false;
  GrinderTcp.close_pending = true;
  GrinderTcp.close_at = millis() + GRINDER_TCP_CLOSE_GRACE;
}

void GrinderTcpFinishActiveClose(void) {
  if (GrinderTcp.client_open && GrinderTcp.close_pending && TimeReached(GrinderTcp.close_at)) {
    GrinderTcpResetActiveClient();
  }
}

void GrinderTcpScheduleClosingClient(WiFiClient &client) {
  for (uint32_t i = 0; i < GRINDER_TCP_BUSY_CLOSE_SLOTS; i++) {
    if (!GrinderTcp.closing[i].open) {
      GrinderTcp.closing[i].client = client;
      GrinderTcp.closing[i].client.setNoDelay(true);
      GrinderTcp.closing[i].close_at = millis() + GRINDER_TCP_CLOSE_GRACE;
      GrinderTcp.closing[i].open = true;
      return;
    }
  }
  client.stop();
}

void GrinderTcpFinishClosingClients(void) {
  for (uint32_t i = 0; i < GRINDER_TCP_BUSY_CLOSE_SLOTS; i++) {
    if (GrinderTcp.closing[i].open && (!GrinderTcp.closing[i].client.connected() || TimeReached(GrinderTcp.closing[i].close_at))) {
      GrinderTcp.closing[i].client.stop();
      GrinderTcp.closing[i].open = false;
      GrinderTcp.closing[i].close_at = 0;
    }
  }
}

void GrinderTcpAccept(WiFiClient &client) {
  GrinderTcp.client = client;
  GrinderTcp.client.setNoDelay(true);
  GrinderTcp.client_open = true;
  GrinderTcp.greeted = false;
  GrinderTcp.authorized_on = false;
  GrinderTcp.close_pending = false;
  GrinderTcp.last_rx = millis();
  GrinderTcp.close_at = 0;
  GrinderTcpLineReset(&GrinderTcp.reader);
}

void GrinderTcpRejectBusy(WiFiClient &client) {
  client.setNoDelay(true);
  GrinderTcpWriteBusy(client);
  GrinderTcpScheduleClosingClient(client);
}

void GrinderTcpProcessLine(const char *line) {
  const GrinderTcpParseResult result = GrinderTcpParseLine(line, GrinderTcp.greeted);
  GrinderTcp.last_rx = millis();
  if (GRINDER_TCP_REASON_NONE != result.reason) {
    GrinderTcpRelayOff();
    GrinderTcpWriteErr(GrinderTcp.client, result.reason);
    GrinderTcpScheduleActiveClose(false);
    return;
  }
  switch (result.action) {
    case GRINDER_TCP_ACTION_HELLO:
      GrinderTcp.greeted = true;
      GrinderTcp.authorized_on = false;
      GrinderTcpWriteOk(GrinderTcp.client);
      break;
    case GRINDER_TCP_ACTION_PING:
      GrinderTcpWriteOk(GrinderTcp.client);
      break;
    case GRINDER_TCP_ACTION_OFF:
      GrinderTcpRelayOff();
      GrinderTcpWriteOk(GrinderTcp.client);
      break;
    case GRINDER_TCP_ACTION_ON:
      GrinderTcpRelayOnCommand();
      GrinderTcpWriteOk(GrinderTcp.client);
      break;
    case GRINDER_TCP_ACTION_STATE:
      GrinderTcpWriteOk(GrinderTcp.client);
      break;
    case GRINDER_TCP_ACTION_BYE:
      GrinderTcpRelayOff();
      GrinderTcpWriteOk(GrinderTcp.client);
      GrinderTcpScheduleActiveClose(false);
      break;
    default:
      GrinderTcpRelayOff();
      GrinderTcpWriteErr(GrinderTcp.client, GRINDER_TCP_REASON_UNKNOWN_COMMAND);
      GrinderTcpScheduleActiveClose(false);
      break;
  }
}

void GrinderTcpProcessEmergencyOff(void) {
  GrinderTcp.last_rx = millis();
  if (!GrinderTcp.greeted) {
    GrinderTcpRelayOff();
    GrinderTcpWriteErr(GrinderTcp.client, GRINDER_TCP_REASON_BEFORE_HELLO);
    GrinderTcpScheduleActiveClose(false);
    return;
  }
  GrinderTcpRelayOff();
  GrinderTcpWriteOk(GrinderTcp.client);
}

void GrinderTcpReadClient(void) {
  if (!GrinderTcp.client_open || GrinderTcp.close_pending) {
    return;
  }
  uint32_t processed = 0;
  while (GrinderTcp.client_open && !GrinderTcp.close_pending && GrinderTcp.client.available() && (processed < GRINDER_TCP_MAX_BYTES_PER_LOOP)) {
    const int value = GrinderTcp.client.read();
    processed++;
    const GrinderTcpReadResult result = GrinderTcpLineRead(&GrinderTcp.reader, (uint8_t)value);
    if (GRINDER_TCP_READ_LINE == result) {
      GrinderTcpProcessLine(GrinderTcp.reader.line);
      GrinderTcpLineReset(&GrinderTcp.reader);
    } else if (GRINDER_TCP_READ_EMERGENCY_OFF == result) {
      GrinderTcpProcessEmergencyOff();
    } else if (GRINDER_TCP_READ_OVERFLOW == result) {
      GrinderTcpRelayOff();
      GrinderTcpWriteErr(GrinderTcp.client, GRINDER_TCP_REASON_LINE_OVERFLOW);
      GrinderTcpScheduleActiveClose(false);
    } else if (GRINDER_TCP_READ_INVALID == result) {
      GrinderTcpRelayOff();
      GrinderTcpWriteErr(GrinderTcp.client, GRINDER_TCP_REASON_INVALID_CHAR);
      GrinderTcpScheduleActiveClose(false);
    }
  }
}

void GrinderTcpPollServer(void) {
  if (!GrinderTcp.server_open) {
    return;
  }
  uint32_t accepted = 0;
  while ((accepted < GRINDER_TCP_ACCEPT_LIMIT) && GrinderTcpServer.hasClient()) {
    WiFiClient client = GrinderTcpServer.accept();
    if (!client) {
      return;
    }
    accepted++;
    if (GrinderTcp.client_open) {
      GrinderTcpRejectBusy(client);
    } else {
      GrinderTcpAccept(client);
    }
  }
}

void GrinderTcpCheckTimeout(void) {
  if (!GrinderTcp.client_open || GrinderTcp.close_pending) {
    return;
  }
  if (!GrinderTcp.client.connected()) {
    GrinderTcpCloseActiveNow(true);
    return;
  }
  const uint32_t timeout = GrinderTcp.greeted ? GRINDER_TCP_HEARTBEAT_TIMEOUT : GRINDER_TCP_HELLO_TIMEOUT;
  if (TimeReached(GrinderTcp.last_rx + timeout)) {
    GrinderTcpCloseActiveNow(true);
  }
}

void GrinderTcpCheckMaxOn(void) {
  if (!GrinderTcpRelayStateOn()) {
    GrinderTcp.on_since = 0;
    return;
  }
  if (GrinderTcpRelayOwned() && TimeReached(GrinderTcp.on_since + GRINDER_TCP_MAX_ON_MS)) {
    GrinderTcpRelayOff();
    GrinderTcpScheduleActiveClose(false);
  }
}

void GrinderTcpEnforceRelayOwnership(void) {
  if (GrinderTcpRelayStateOn()) {
    if (!GrinderTcpRelayOwned()) {
      GrinderTcpRelayOffDirect();
    }
  } else {
    GrinderTcp.authorized_on = false;
  }
}

void GrinderTcpKeepAwakeWhileGrinding(void) {
  if (GrinderTcpRelayOwned() && GrinderTcpRelayStateOn() && (TasmotaGlobal.skip_sleep < 1)) {
    TasmotaGlobal.skip_sleep = 1;
  }
}

void GrinderTcpLoop(void) {
  GrinderTcpFinishActiveClose();
  GrinderTcpFinishClosingClients();
  if (GrinderTcp.server_open && !GrinderTcpRelayHardwareSupported()) {
    GrinderTcpStop();
    return;
  }
  GrinderTcpReadClient();
  GrinderTcpCheckTimeout();
  GrinderTcpEnforceRelayOwnership();
  GrinderTcpCheckMaxOn();
  GrinderTcpKeepAwakeWhileGrinding();
  GrinderTcpPollServer();
  if (GrinderTcp.server_open && !GrinderTcpRelayStateOn()) {
    if (!Mdns.begun) {
      GrinderTcp.advertised = false;
    }
    GrinderTcpAdvertise();
  }
}

void GrinderTcpEnsureMdns(void) {
  Settings->flag3.mdns_enabled = 1;
  const bool was_begun = Mdns.begun;
  if (!Mdns.begun) {
    StartMdns();
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

void GrinderTcpAdvertise(void) {
  if (!TimeReached(GrinderTcp.mdns_retry_at)) {
    return;
  }
  GrinderTcp.mdns_retry_at = millis() + GRINDER_TCP_MDNS_RETRY;
  GrinderTcpEnsureMdns();
  if (!Mdns.begun) {
    return;
  }
  if (GrinderTcp.advertised) {
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
  AddLog(LOG_LEVEL_INFO,
         PSTR("GTC: mDNS service %u txt %u host %s mac %s port %u"),
         service_added,
         txt_added,
         NetworkHostname(),
         GrinderTcp.plug_mac,
         GRINDER_TCP_PORT);
  if (service_added && txt_added) {
    GrinderTcp.advertised = true;
  }
}

void GrinderTcpStart(void) {
  if (!GrinderTcpCacheIdentity()) {
    GrinderTcpRelayOff();
    AddLog(LOG_LEVEL_ERROR, PSTR("GTC: Invalid MAC"));
    return;
  }
  if (!GrinderTcpRelayHardwareSupported()) {
    GrinderTcpRelayOff();
    AddLog(LOG_LEVEL_ERROR, PSTR("GTC: Unsupported relay layout"));
    return;
  }
  if (GrinderTcp.server_open) {
    return;
  }
  GrinderTcpNeutralizePowerDelay();
  GrinderTcpServer.begin();
  GrinderTcpServer.setNoDelay(true);
  GrinderTcp.server_open = true;
  GrinderTcp.mdns_retry_at = 0;
  GrinderTcpAdvertise();
  AddLogServerActive(PSTR("Grinder TCP"));
}

void GrinderTcpStopClosingClients(void) {
  for (uint32_t i = 0; i < GRINDER_TCP_BUSY_CLOSE_SLOTS; i++) {
    if (GrinderTcp.closing[i].open) {
      GrinderTcp.closing[i].client.stop();
      GrinderTcp.closing[i].open = false;
      GrinderTcp.closing[i].close_at = 0;
    }
  }
}

void GrinderTcpStop(void) {
  GrinderTcpCloseActiveNow(true);
  GrinderTcpStopClosingClients();
  if (GrinderTcp.server_open) {
    GrinderTcpServer.stop();
    GrinderTcp.server_open = false;
  }
  GrinderTcpRemoveMdnsService();
}

void GrinderTcpPreInit(void) {
  GrinderTcpApplyQuietSettings();
  Settings->poweronstate = POWER_ALL_OFF;
  Settings->power = 0;
  TasmotaGlobal.power = 0;
  TasmotaGlobal.last_power = 0;
  GrinderTcp.authorized_on = false;
  GrinderTcpNeutralizePowerDelay();
}

void GrinderTcpInit(void) {
  GrinderTcpApplyQuietSettings();
  GrinderTcpCacheIdentity();
  GrinderTcpLineReset(&GrinderTcp.reader);
  GrinderTcpNeutralizePowerDelay();
  GrinderTcpRelayOff();
}

void GrinderTcpObservePower(void) {
  if (!(XdrvMailbox.index & 1)) {
    GrinderTcp.authorized_on = false;
  }
}

bool GrinderTcpSetDevicePowerGuard(power_t rpower, uint32_t source) {
  (void)source;
  if (!(rpower & 1)) {
    GrinderTcp.authorized_on = false;
    return false;
  }
  if (GrinderTcp.tcp_power_command && GrinderTcpRelayOwned()) {
    return false;
  }
  GrinderTcpRelayOffDirect();
  return true;
}

bool GrinderTcpSetDevicePower(void) {
  return GrinderTcpSetDevicePowerGuard(XdrvMailbox.index, XdrvMailbox.payload);
}

bool Xdrv95(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_PRE_INIT:
      GrinderTcpPreInit();
      break;
    case FUNC_INIT:
      GrinderTcpInit();
      break;
    case FUNC_NETWORK_UP:
      if (!TasmotaGlobal.restart_flag) {
        GrinderTcpApplyQuietSettings();
        GrinderTcpStart();
      }
      break;
    case FUNC_LOOP:
      GrinderTcpLoop();
      break;
    case FUNC_NETWORK_DOWN:
    case FUNC_SAVE_BEFORE_RESTART:
      GrinderTcpStop();
      break;
    case FUNC_SET_POWER:
      GrinderTcpObservePower();
      break;
    case FUNC_SET_DEVICE_POWER:
      result = GrinderTcpSetDevicePower();
      break;
    case FUNC_ACTIVE:
      result = GrinderTcp.server_open || GrinderTcp.client_open;
      break;
  }
  return result;
}

#endif
