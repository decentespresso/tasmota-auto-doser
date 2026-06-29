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
#define GRINDER_TCP_HEARTBEAT_TIMEOUT 1500
#endif

#ifndef GRINDER_TCP_HELLO_TIMEOUT
#define GRINDER_TCP_HELLO_TIMEOUT 1000
#endif

#ifndef GRINDER_TCP_CLOSE_GRACE
#define GRINDER_TCP_CLOSE_GRACE 50
#endif

#ifndef GRINDER_TCP_ACCEPT_LIMIT
#define GRINDER_TCP_ACCEPT_LIMIT 4
#endif

#ifndef GRINDER_TCP_BUSY_CLOSE_SLOTS
#define GRINDER_TCP_BUSY_CLOSE_SLOTS 4
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

void GrinderTcpRelayOffDirect(void) {
  GrinderTcp.authorized_on = false;
  TasmotaGlobal.power &= (POWER_MASK ^ 1);
  TasmotaGlobal.blink_mask &= (POWER_MASK ^ 1);
  if (PinUsed(GPIO_REL1, 0)) {
    DigitalWrite(GPIO_REL1, 0, bitRead(TasmotaGlobal.rel_inverted, 0) ? 1 : 0);
  }
}

void GrinderTcpRelayOff(void) {
  GrinderTcp.authorized_on = false;
  ExecuteCommandPower(1, POWER_OFF_FORCE, SRC_IGNORE);
  GrinderTcpRelayOffDirect();
}

void GrinderTcpRelayOnCommand(void) {
  if (!GrinderTcp.client_open || !GrinderTcp.greeted || GrinderTcp.close_pending) {
    GrinderTcpRelayOff();
    return;
  }
  GrinderTcp.authorized_on = true;
  GrinderTcp.tcp_power_command = true;
  ExecuteCommandPower(1, POWER_ON, SRC_IGNORE);
  GrinderTcp.tcp_power_command = false;
  if (!GrinderTcpRelayStateOn()) {
    GrinderTcp.authorized_on = false;
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

void GrinderTcpReadClient(void) {
  if (!GrinderTcp.client_open || GrinderTcp.close_pending) {
    return;
  }
  while (GrinderTcp.client_open && !GrinderTcp.close_pending && GrinderTcp.client.available()) {
    const int value = GrinderTcp.client.read();
    const GrinderTcpReadResult result = GrinderTcpLineRead(&GrinderTcp.reader, (uint8_t)value);
    if (GRINDER_TCP_READ_LINE == result) {
      GrinderTcpProcessLine(GrinderTcp.reader.line);
      GrinderTcpLineReset(&GrinderTcp.reader);
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

void GrinderTcpEnforceRelayOwnership(void) {
  if (GrinderTcpRelayStateOn()) {
    if (!GrinderTcpRelayOwned()) {
      GrinderTcpRelayOffDirect();
    }
  } else {
    GrinderTcp.authorized_on = false;
  }
}

void GrinderTcpLoop(void) {
  GrinderTcpFinishActiveClose();
  GrinderTcpFinishClosingClients();
  if (GrinderTcp.server_open && !GrinderTcp.advertised) {
    GrinderTcpAdvertise();
  }
  GrinderTcpReadClient();
  GrinderTcpCheckTimeout();
  GrinderTcpPollServer();
  GrinderTcpEnforceRelayOwnership();
}

void GrinderTcpAdvertise(void) {
  if (!GrinderTcp.advertised && Mdns.begun) {
    char service[] = "grinderplug";
    char proto[] = "tcp";
    char key_mac[] = "mac";
    char key_name[] = "name";
    char key_model[] = "model";
    char key_proto[] = "proto";
    char model[] = GRINDER_TCP_MODEL;
    char proto_version[] = "1";
    const bool service_added = MDNS.addService(service, proto, GRINDER_TCP_PORT);
    if (service_added) {
      MDNS.addServiceTxt(service, proto, key_mac, GrinderTcp.plug_mac);
      MDNS.addServiceTxt(service, proto, key_name, NetworkHostname());
      MDNS.addServiceTxt(service, proto, key_model, model);
      MDNS.addServiceTxt(service, proto, key_proto, proto_version);
      GrinderTcp.advertised = true;
    }
  }
}

void GrinderTcpStart(void) {
  if (!GrinderTcpCacheIdentity()) {
    GrinderTcpRelayOff();
    AddLog(LOG_LEVEL_ERROR, PSTR("GTC: Invalid MAC"));
    return;
  }
  if (GrinderTcp.server_open) {
    return;
  }
  GrinderTcpServer.begin();
  GrinderTcpServer.setNoDelay(true);
  GrinderTcp.server_open = true;
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
  GrinderTcp.advertised = false;
}

void GrinderTcpPreInit(void) {
  Settings->poweronstate = POWER_ALL_OFF;
  Settings->power = 0;
  TasmotaGlobal.power = 0;
  Settings->flag3.mdns_enabled = 1;
  GrinderTcp.authorized_on = false;
}

void GrinderTcpInit(void) {
  GrinderTcpCacheIdentity();
  GrinderTcpLineReset(&GrinderTcp.reader);
  GrinderTcpRelayOff();
}

void GrinderTcpObservePower(void) {
  if (!(XdrvMailbox.index & 1)) {
    GrinderTcp.authorized_on = false;
  }
}

bool GrinderTcpSetDevicePower(void) {
  if (!(XdrvMailbox.index & 1)) {
    GrinderTcp.authorized_on = false;
    return false;
  }
  if (GrinderTcp.tcp_power_command && GrinderTcpRelayOwned()) {
    return false;
  }
  GrinderTcpRelayOffDirect();
  return true;
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
