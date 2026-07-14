#ifdef USE_GRINDER_TCP

#ifndef USE_DISCOVERY
#error USE_GRINDER_TCP requires USE_DISCOVERY
#endif

#ifndef GRINDER_TCP_EARLY_POWER_GUARD_INSTALLED
#error USE_GRINDER_TCP requires the early SetDevicePower guard
#endif

#ifndef ESP32
#error USE_GRINDER_TCP requires ESP32
#endif

#ifdef XDRV_95
#error XDRV_95 already defined
#endif

#define XDRV_95 95

#include <errno.h>
#include <sys/socket.h>
#include "esp_mac.h"

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

#ifndef GRINDER_TCP_TX_TIMEOUT
#define GRINDER_TCP_TX_TIMEOUT 250
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

#ifndef GRINDER_TCP_MDNS_REFRESH
#define GRINDER_TCP_MDNS_REFRESH 60000
#endif

#ifndef GRINDER_TCP_MAX_BYTES_PER_LOOP
#define GRINDER_TCP_MAX_BYTES_PER_LOOP 256
#endif

#ifndef GRINDER_TCP_IDENTITY_CHECK
#define GRINDER_TCP_IDENTITY_CHECK 5000
#endif

#ifndef GRINDER_TCP_MODEL
#define GRINDER_TCP_MODEL "NOUS_A6T"
#endif

struct GrinderTcpPendingTx {
  char data[66] = { 0 };
  uint32_t length = 0;
  uint32_t sent = 0;
  uint32_t deadline = 0;
  bool close_after = false;
};

enum GrinderTcpTxResult {
  GRINDER_TCP_TX_PENDING,
  GRINDER_TCP_TX_COMPLETE,
  GRINDER_TCP_TX_FAILED
};

void GrinderTcpResetTx(GrinderTcpPendingTx &tx);
bool GrinderTcpQueueTx(GrinderTcpPendingTx &tx, const char *line, const bool close_after);
GrinderTcpTxResult GrinderTcpFlushTx(WiFiClient &client, GrinderTcpPendingTx &tx);
bool GrinderTcpQueueBusy(GrinderTcpPendingTx &tx);

struct GrinderTcpClosingClient {
  WiFiClient client;
  GrinderTcpPendingTx tx;
  uint32_t close_at = 0;
  bool open = false;
};

WiFiServer GrinderTcpServer(GRINDER_TCP_PORT);

struct {
  WiFiClient client;
  GrinderTcpClosingClient closing[GRINDER_TCP_BUSY_CLOSE_SLOTS];
  GrinderTcpLineReader reader;
  GrinderTcpPendingTx tx;
  uint32_t last_rx = 0;
  uint32_t close_at = 0;
  uint32_t mdns_retry_at = 0;
  uint32_t mdns_refresh_at = 0;
  char plug_mac[18] = { 0 };
  bool server_started = false;
  bool client_open = false;
  bool greeted = false;
  bool advertised = false;
  bool authorized_on = false;
  bool close_pending = false;
  bool tcp_power_command = false;
} GrinderTcp;

void GrinderTcpRestartServer(const char *reason);
void GrinderTcpStop(const char *reason);
void GrinderTcpCheckNetwork(void);
void GrinderTcpAdvertise(const bool force = false);

#include "tasmota_xdrv_driver/xdrv_95_grinder_tcp_diagnostics.h"

void GrinderTcpApplyQuietSettings(void) {
  const bool corrected = Settings->deepsleep || !Settings->flag5.wifi_no_sleep || Settings->flag3.use_wifi_rescan;
  Settings->deepsleep = 0;
  Settings->flag5.wifi_no_sleep = 1;
  Settings->flag3.use_wifi_rescan = 0;
  if (corrected) {
    AddLog(LOG_LEVEL_INFO, PSTR("GTC: Enforced DeepSleep 0, SetOption127 1, SetOption57 0"));
  }
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

void GrinderTcpNeutralizePowerControls(void) {
  Settings->power_lock &= (POWER_MASK ^ 1);
  Settings->pulse_timer[0] = 0;
  TasmotaGlobal.pulse_timer[0] = 0;
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

void GrinderTcpRelayOffDirect(const char *reason = nullptr) {
  if (reason) {
    GrinderTcpRecordFailSafe(reason);
  }
  GrinderTcp.authorized_on = false;
  GrinderTcpNeutralizePowerControls();
  TasmotaGlobal.power &= (POWER_MASK ^ 1);
  TasmotaGlobal.last_power &= (POWER_MASK ^ 1);
  TasmotaGlobal.blink_mask &= (POWER_MASK ^ 1);
  if (PinUsed(GPIO_REL1, 0)) {
    DigitalWrite(GPIO_REL1, 0, bitRead(TasmotaGlobal.rel_inverted, 0) ? 1 : 0);
  }
}

void GrinderTcpRelayOff(const char *reason = "requested_off") {
  GrinderTcpRecordFailSafe(reason);
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
  GrinderTcpNeutralizePowerControls();
  GrinderTcp.authorized_on = true;
  GrinderTcp.tcp_power_command = true;
  ExecuteCommandPower(1, POWER_ON, SRC_IGNORE);
  GrinderTcp.tcp_power_command = false;
  if (!GrinderTcpRelayStateOn()) {
    GrinderTcp.authorized_on = false;
  }
}

void GrinderTcpResetTx(GrinderTcpPendingTx &tx) {
  tx.length = 0;
  tx.sent = 0;
  tx.deadline = 0;
  tx.close_after = false;
}

bool GrinderTcpQueueTx(GrinderTcpPendingTx &tx, const char *line, const bool close_after) {
  const size_t length = strlen(line);
  if (tx.length || ((length + 1) > sizeof(tx.data))) {
    return false;
  }
  memcpy(tx.data, line, length);
  tx.data[length] = '\n';
  tx.length = length + 1;
  tx.sent = 0;
  tx.deadline = millis() + GRINDER_TCP_TX_TIMEOUT;
  tx.close_after = close_after;
  return true;
}

GrinderTcpTxResult GrinderTcpFlushTx(WiFiClient &client, GrinderTcpPendingTx &tx) {
  if (!client.connected() || TimeReached(tx.deadline)) {
    return GRINDER_TCP_TX_FAILED;
  }
  const int socket = client.fd();
  if (socket < 0) {
    return GRINDER_TCP_TX_FAILED;
  }
  const ssize_t written = send(socket, tx.data + tx.sent, tx.length - tx.sent, MSG_DONTWAIT);
  if (written > 0) {
    tx.sent += written;
    return (tx.sent == tx.length) ? GRINDER_TCP_TX_COMPLETE : GRINDER_TCP_TX_PENDING;
  }
  if ((written < 0) && ((EAGAIN == errno) || (EWOULDBLOCK == errno) || (EINTR == errno))) {
    return GRINDER_TCP_TX_PENDING;
  }
  return GRINDER_TCP_TX_FAILED;
}

bool GrinderTcpQueueOk(const bool close_after = false) {
  char response[48];
  GrinderTcpFormatOk(response, sizeof(response), GrinderTcp.plug_mac, GrinderTcpRelayStateOn());
  return GrinderTcpQueueTx(GrinderTcp.tx, response, close_after);
}

bool GrinderTcpQueueBusy(GrinderTcpPendingTx &tx) {
  char response[32];
  GrinderTcpFormatBusy(response, sizeof(response), GrinderTcp.plug_mac);
  return GrinderTcpQueueTx(tx, response, true);
}

bool GrinderTcpQueueErr(const uint32_t reason) {
  char response[64];
  GrinderTcpFormatErr(response, sizeof(response), GrinderTcp.plug_mac, (GrinderTcpReason)reason);
  return GrinderTcpQueueTx(GrinderTcp.tx, response, true);
}

void GrinderTcpResetActiveClient(const char *reason) {
  if (GrinderTcp.client_open) {
    GrinderTcpDiag.active_disconnects++;
    strlcpy(GrinderTcpDiag.last_close_reason, reason, sizeof(GrinderTcpDiag.last_close_reason));
    GrinderTcpRecordEvent(reason);
  }
  GrinderTcp.client.stop();
  GrinderTcp.client_open = false;
  GrinderTcp.greeted = false;
  GrinderTcp.authorized_on = false;
  GrinderTcp.close_pending = false;
  GrinderTcp.last_rx = 0;
  GrinderTcp.close_at = 0;
  GrinderTcpDiag.active_remote_ip = 0;
  GrinderTcpDiag.active_remote_port = 0;
  GrinderTcpLineReset(&GrinderTcp.reader);
  GrinderTcpResetTx(GrinderTcp.tx);
}

void GrinderTcpCloseActiveNow(const bool relay_off, const char *reason = "disconnect") {
  if (relay_off) {
    GrinderTcpRelayOff(reason);
  }
  if (GrinderTcp.client_open) {
    GrinderTcpResetActiveClient(reason);
  }
}

void GrinderTcpScheduleActiveClose(const bool relay_off, const char *reason = "protocol_close") {
  if (relay_off) {
    GrinderTcpRelayOff(reason);
  }
  if (!GrinderTcp.client_open) {
    GrinderTcpResetActiveClient(reason);
    return;
  }
  strlcpy(GrinderTcpDiag.last_close_reason, reason, sizeof(GrinderTcpDiag.last_close_reason));
  GrinderTcp.authorized_on = false;
  GrinderTcp.close_pending = true;
  GrinderTcp.close_at = millis() + GRINDER_TCP_CLOSE_GRACE;
}

void GrinderTcpFinishActiveClose(void) {
  if (GrinderTcp.client_open && GrinderTcp.close_pending && TimeReached(GrinderTcp.close_at)) {
    GrinderTcpResetActiveClient(GrinderTcpDiag.last_close_reason);
  }
}

void GrinderTcpFlushActiveTx(void) {
  if (!GrinderTcp.client_open || !GrinderTcp.tx.length) {
    return;
  }
  const bool close_after = GrinderTcp.tx.close_after;
  const GrinderTcpTxResult result = GrinderTcpFlushTx(GrinderTcp.client, GrinderTcp.tx);
  if (GRINDER_TCP_TX_PENDING == result) {
    return;
  }
  GrinderTcpResetTx(GrinderTcp.tx);
  if (GRINDER_TCP_TX_FAILED == result) {
    GrinderTcpCloseActiveNow(true);
  } else if (close_after) {
    GrinderTcpScheduleActiveClose(false);
  }
}

void GrinderTcpQueueActiveErrAndClose(const uint32_t reason) {
  GrinderTcpDiag.protocol_errors++;
  GrinderTcpRelayOff("protocol_error");
  if (!GrinderTcpQueueErr(reason)) {
    GrinderTcpCloseActiveNow(false);
  }
}

void GrinderTcpScheduleClosingClient(WiFiClient &client) {
  for (uint32_t i = 0; i < GRINDER_TCP_BUSY_CLOSE_SLOTS; i++) {
    if (!GrinderTcp.closing[i].open) {
      GrinderTcp.closing[i].client = client;
      GrinderTcp.closing[i].client.setNoDelay(true);
      GrinderTcp.closing[i].open = true;
      GrinderTcp.closing[i].close_at = 0;
      GrinderTcpResetTx(GrinderTcp.closing[i].tx);
      if (!GrinderTcpQueueBusy(GrinderTcp.closing[i].tx)) {
        GrinderTcp.closing[i].client.stop();
        GrinderTcp.closing[i].open = false;
      }
      return;
    }
  }
  client.stop();
}

void GrinderTcpFinishClosingClients(void) {
  for (uint32_t i = 0; i < GRINDER_TCP_BUSY_CLOSE_SLOTS; i++) {
    if (!GrinderTcp.closing[i].open) {
      continue;
    }
    if (!GrinderTcp.closing[i].client.connected()) {
      GrinderTcp.closing[i].client.stop();
      GrinderTcp.closing[i].open = false;
      GrinderTcp.closing[i].close_at = 0;
      GrinderTcpResetTx(GrinderTcp.closing[i].tx);
      continue;
    }
    if (GrinderTcp.closing[i].tx.length) {
      const GrinderTcpTxResult result = GrinderTcpFlushTx(GrinderTcp.closing[i].client, GrinderTcp.closing[i].tx);
      if (GRINDER_TCP_TX_PENDING == result) {
        continue;
      }
      GrinderTcpResetTx(GrinderTcp.closing[i].tx);
      if (GRINDER_TCP_TX_FAILED == result) {
        GrinderTcp.closing[i].client.stop();
        GrinderTcp.closing[i].open = false;
        continue;
      }
      GrinderTcp.closing[i].close_at = millis() + GRINDER_TCP_CLOSE_GRACE;
    }
    if (TimeReached(GrinderTcp.closing[i].close_at)) {
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
  GrinderTcpResetTx(GrinderTcp.tx);
  GrinderTcpDiag.accepted_clients++;
  GrinderTcpDiag.active_remote_ip = (uint32_t)GrinderTcp.client.remoteIP();
  GrinderTcpDiag.active_remote_port = GrinderTcp.client.remotePort();
  GrinderTcpRecordEvent("client_accepted");
}

void GrinderTcpRejectBusy(WiFiClient &client) {
  GrinderTcpDiag.busy_clients++;
  GrinderTcpRecordEvent("client_busy");
  GrinderTcpScheduleClosingClient(client);
}

void GrinderTcpProcessLine(const char *line) {
  const GrinderTcpParseResult result = GrinderTcpParseLine(line, GrinderTcp.greeted);
  GrinderTcp.last_rx = millis();
  if (GRINDER_TCP_REASON_NONE != result.reason) {
    GrinderTcpQueueActiveErrAndClose(result.reason);
    return;
  }
  bool close_after_response = false;
  switch (result.action) {
    case GRINDER_TCP_ACTION_HELLO:
      GrinderTcp.greeted = true;
      GrinderTcp.authorized_on = false;
      break;
    case GRINDER_TCP_ACTION_PING:
      break;
    case GRINDER_TCP_ACTION_OFF:
      GrinderTcpRelayOff();
      break;
    case GRINDER_TCP_ACTION_ON:
      GrinderTcpRelayOnCommand();
      break;
    case GRINDER_TCP_ACTION_STATE:
      break;
    case GRINDER_TCP_ACTION_BYE:
      GrinderTcpRelayOff();
      close_after_response = true;
      break;
    default:
      GrinderTcpQueueActiveErrAndClose(GRINDER_TCP_REASON_UNKNOWN_COMMAND);
      return;
  }
  if (!GrinderTcpQueueOk(close_after_response)) {
    GrinderTcpCloseActiveNow(true);
  }
}

void GrinderTcpProcessEmergencyOff(void) {
  GrinderTcp.last_rx = millis();
  if (!GrinderTcp.greeted) {
    GrinderTcpQueueActiveErrAndClose(GRINDER_TCP_REASON_BEFORE_HELLO);
    return;
  }
  GrinderTcpRelayOff();
  if (!GrinderTcpQueueOk()) {
    GrinderTcpCloseActiveNow(false);
  }
}

void GrinderTcpReadClient(void) {
  if (!GrinderTcp.client_open || GrinderTcp.close_pending || GrinderTcp.tx.length) {
    return;
  }
  uint32_t processed = 0;
  while (GrinderTcp.client_open && !GrinderTcp.close_pending && !GrinderTcp.tx.length && GrinderTcp.client.available() && (processed < GRINDER_TCP_MAX_BYTES_PER_LOOP)) {
    const int value = GrinderTcp.client.read();
    processed++;
    const GrinderTcpReadResult result = GrinderTcpLineRead(&GrinderTcp.reader, (uint8_t)value);
    if (GRINDER_TCP_READ_LINE == result) {
      GrinderTcpProcessLine(GrinderTcp.reader.line);
      GrinderTcpLineReset(&GrinderTcp.reader);
    } else if (GRINDER_TCP_READ_EMERGENCY_OFF == result) {
      GrinderTcpProcessEmergencyOff();
    } else if (GRINDER_TCP_READ_OVERFLOW == result) {
      GrinderTcpQueueActiveErrAndClose(GRINDER_TCP_REASON_LINE_OVERFLOW);
    } else if (GRINDER_TCP_READ_INVALID == result) {
      GrinderTcpQueueActiveErrAndClose(GRINDER_TCP_REASON_INVALID_CHAR);
    }
  }
}

void GrinderTcpPollServer(void) {
  if (!GrinderTcp.server_started) {
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
    GrinderTcpCloseActiveNow(true, "socket_closed");
    return;
  }
  const uint32_t timeout = GrinderTcp.greeted ? GRINDER_TCP_HEARTBEAT_TIMEOUT : GRINDER_TCP_HELLO_TIMEOUT;
  if (TimeReached(GrinderTcp.last_rx + timeout)) {
    if (GrinderTcp.greeted) {
      GrinderTcpDiag.heartbeat_timeouts++;
      GrinderTcpCloseActiveNow(true, "heartbeat_timeout");
    } else {
      GrinderTcpDiag.hello_timeouts++;
      GrinderTcpCloseActiveNow(true, "hello_timeout");
    }
  }
}

void GrinderTcpEnforceRelayOwnership(void) {
  if (GrinderTcpRelayStateOn()) {
    if (!GrinderTcpRelayOwned()) {
      GrinderTcpRelayOffDirect("ownership_lost");
    }
  } else {
    GrinderTcp.authorized_on = false;
  }
}

void GrinderTcpKeepAwakeWhileConnected(void) {
  if (GrinderTcp.client_open && GrinderTcp.greeted && !GrinderTcp.close_pending && (TasmotaGlobal.skip_sleep < 1)) {
    TasmotaGlobal.skip_sleep = 1;
  }
}

void GrinderTcpLoop(void) {
  GrinderTcpObserveDiagnostics();
  GrinderTcpCheckNetwork();
  GrinderTcpFinishActiveClose();
  GrinderTcpFinishClosingClients();
  GrinderTcpFlushActiveTx();
  if (GrinderTcp.server_started && !GrinderTcpRelayHardwareSupported()) {
    GrinderTcpStop("unsupported_layout");
    return;
  }
  GrinderTcpReadClient();
  GrinderTcpFlushActiveTx();
  GrinderTcpCheckTimeout();
  GrinderTcpEnforceRelayOwnership();
  GrinderTcpKeepAwakeWhileConnected();
  GrinderTcpPollServer();
  const bool authenticated = GrinderTcp.client_open && GrinderTcp.greeted && !GrinderTcp.close_pending;
  if (GrinderTcp.server_started && !GrinderTcpRelayStateOn() && !authenticated) {
    if (!Mdns.begun) {
      GrinderTcp.advertised = false;
      GrinderTcp.mdns_refresh_at = 0;
    }
    const bool refresh = GrinderTcp.advertised && TimeReached(GrinderTcp.mdns_refresh_at);
    if (!GrinderTcp.advertised || refresh) {
      GrinderTcpAdvertise(refresh);
    }
  }
}

#include "tasmota_xdrv_driver/xdrv_95_grinder_tcp_recovery.h"

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
  if (GrinderTcp.server_started) {
    return;
  }
  GrinderTcpNeutralizePowerControls();
  GrinderTcpServer.begin();
  GrinderTcpServer.setNoDelay(true);
  GrinderTcp.server_started = true;
  GrinderTcpDiag.server_starts++;
  GrinderTcpDiag.server_generation++;
  GrinderTcpRecordEvent("server_started");
  GrinderTcp.mdns_retry_at = 0;
  GrinderTcp.mdns_refresh_at = 0;
  GrinderTcpAdvertise();
  AddLogServerActive(PSTR("Grinder TCP"));
}

void GrinderTcpStopClosingClients(void) {
  for (uint32_t i = 0; i < GRINDER_TCP_BUSY_CLOSE_SLOTS; i++) {
    if (GrinderTcp.closing[i].open) {
      GrinderTcp.closing[i].client.stop();
      GrinderTcp.closing[i].open = false;
      GrinderTcp.closing[i].close_at = 0;
      GrinderTcpResetTx(GrinderTcp.closing[i].tx);
    }
  }
}

void GrinderTcpStop(const char *reason) {
  GrinderTcpRelayOff(reason);
  GrinderTcpCloseActiveNow(false, reason);
  GrinderTcpStopClosingClients();
  const bool was_started = GrinderTcp.server_started;
  GrinderTcpServer.stop();
  GrinderTcp.server_started = false;
  if (was_started) {
    GrinderTcpDiag.server_stops++;
    GrinderTcpRecordEvent("server_stopped");
  }
  GrinderTcpRemoveMdnsService();
  GrinderTcp.mdns_refresh_at = 0;
}

void GrinderTcpRestartServer(const char *reason) {
  const bool restarting = GrinderTcpDiag.server_generation > 0;
  GrinderTcpStop(reason);
  if (!WifiHasIP()) {
    return;
  }
  if (restarting) {
    GrinderTcpDiag.forced_restarts++;
  }
  AddLog(LOG_LEVEL_INFO, PSTR("GTC: %s reason %s network %u"), restarting ? PSTR("Restart") : PSTR("Start"), reason, GrinderTcpDiag.network_generation);
  GrinderTcpStart();
}

void GrinderTcpPreInit(void) {
  GrinderTcpApplyQuietSettings();
  Settings->poweronstate = POWER_ALL_OFF;
  Settings->power = 0;
  TasmotaGlobal.power = 0;
  TasmotaGlobal.last_power = 0;
  GrinderTcp.authorized_on = false;
  GrinderTcpNeutralizePowerControls();
}

void GrinderTcpInit(void) {
  GrinderTcpApplyQuietSettings();
  GrinderTcpCacheIdentity();
  GrinderTcpLineReset(&GrinderTcp.reader);
  GrinderTcpNeutralizePowerControls();
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
  GrinderTcpRelayOffDirect("external_power_on");
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
        GrinderTcpNetworkUp();
      }
      break;
    case FUNC_LOOP:
      GrinderTcpLoop();
      break;
    case FUNC_NETWORK_DOWN:
      GrinderTcpNetworkDown();
      break;
    case FUNC_SAVE_BEFORE_RESTART:
      GrinderTcpStop("device_restart");
      break;
    case FUNC_COMMAND:
      result = DecodeCommand(kGrinderCommands, GrinderCommand);
      break;
    case FUNC_SET_POWER:
      GrinderTcpObservePower();
      break;
    case FUNC_SET_DEVICE_POWER:
      result = GrinderTcpSetDevicePower();
      break;
    case FUNC_ACTIVE:
      result = GrinderTcp.server_started || GrinderTcp.client_open;
      break;
  }
  return result;
}

#endif
