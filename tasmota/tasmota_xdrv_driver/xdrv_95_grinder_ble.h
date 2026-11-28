#pragma once

#ifndef CONFIG_NIMBLE_CPP_IDF
#define CONFIG_NIMBLE_CPP_IDF 1
#endif
#include <NimBLEDevice.h>
#include "xdrv_95_grinder_ble_mailbox.h"

static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS >= 2, "Grinder BLE requires an owner and a BUSY link");

static constexpr uint32_t GrinderBleHelloTimeout = 2000;
static constexpr uint32_t GrinderBleBusyTimeout = 1500;
static constexpr uint32_t GrinderBleBusyGrace = 250;
static constexpr uint32_t GrinderBleAdvertisingRetry = 100;
static constexpr uint32_t GrinderBleSetupRetry = 5000;

static portMUX_TYPE GrinderBleMux = portMUX_INITIALIZER_UNLOCKED;
static GrinderBleMailbox GrinderBleInput{};

static struct {
  NimBLEServer *server = nullptr;
  NimBLECharacteristic *reply = nullptr;
  NimBLEAdvertising *advertising = nullptr;
  GrinderBleLine line{};
  uint32_t ownerGeneration = 0;
  uint32_t contenderGeneration = 0;
  uint32_t contenderRepliedAt = 0;
  uint32_t advertisingAttemptAt = 0;
  uint32_t setupAttemptAt = 0U - GrinderBleSetupRetry;
  bool setupNeedsReset = false;
  bool contenderReplied = false;
  bool active = false;
} GrinderBle;

class GrinderBleServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connection) override {
    const uint16_t handle = connection.getConnHandle();
    const uint32_t now = millis();
    portENTER_CRITICAL(&GrinderBleMux);
    const bool accepted = GrinderBleInput.connect(handle, now);
    portEXIT_CRITICAL(&GrinderBleMux);
    if (!accepted) {
      server->disconnect(handle);
    }
  }

  void onDisconnect(NimBLEServer *, NimBLEConnInfo &connection, int) override {
    portENTER_CRITICAL(&GrinderBleMux);
    GrinderBleInput.disconnect(connection.getConnHandle());
    portEXIT_CRITICAL(&GrinderBleMux);
  }
};

class GrinderBleCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connection) override {
    const uint32_t receivedAt = millis();
    const NimBLEAttValue value = characteristic->getValue();
    portENTER_CRITICAL(&GrinderBleMux);
    GrinderBleInput.write(connection.getConnHandle(), value.data(), value.size(), receivedAt);
    portEXIT_CRITICAL(&GrinderBleMux);
  }

  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &connection, uint16_t subscription) override {
    portENTER_CRITICAL(&GrinderBleMux);
    GrinderBleInput.subscribe(connection.getConnHandle(), subscription & 1);
    portEXIT_CRITICAL(&GrinderBleMux);
  }
};

static GrinderBleServerCallbacks GrinderBleServerEvents;
static GrinderBleCharacteristicCallbacks GrinderBleCharacteristicEvents;

bool GrinderBleControlActive(void) {
  if (GrinderTcpRelayStateOn()) { return true; }
  portENTER_CRITICAL(&GrinderBleMux);
  const bool connected = GrinderBleInput.owner.connected();
  portEXIT_CRITICAL(&GrinderBleMux);
  return connected;
}

static bool GrinderBleOwnsRelayLink(void) {
  portENTER_CRITICAL(&GrinderBleMux);
  const bool owns = GrinderBleInput.owns(GrinderBle.ownerGeneration);
  portEXIT_CRITICAL(&GrinderBleMux);
  return owns;
}

static bool GrinderBleNotify(const GrinderBlePeer &peer, bool contender, uint8_t id, const char *line) {
  const size_t length = strlen(line);
  for (size_t offset = 0; offset <= length;) {
    portENTER_CRITICAL(&GrinderBleMux);
    const GrinderBlePeer current = contender ? GrinderBleInput.contender : GrinderBleInput.owner;
    portEXIT_CRITICAL(&GrinderBleMux);
    if (current.handle != peer.handle || current.generation != peer.generation ||
        !current.subscribed || current.closing) {
      return false;
    }
    uint8_t value[GrinderBleValueSize] = {id};
    size_t size = 1;
    while (size < sizeof(value) && offset <= length) {
      value[size++] = offset == length ? '\n' : line[offset];
      offset++;
    }
    if (!GrinderBle.reply->notify(value, size, peer.handle)) {
      return false;
    }
  }
  return true;
}

static void GrinderBleDisconnect(const GrinderBlePeer &peer, bool contender) {
  portENTER_CRITICAL(&GrinderBleMux);
  GrinderBlePeer &current = contender ? GrinderBleInput.contender : GrinderBleInput.owner;
  GrinderBleInput.close(current, peer.generation);
  const bool connected = current.connected() && current.generation == peer.generation;
  portEXIT_CRITICAL(&GrinderBleMux);
  if (connected) {
    GrinderBle.server->disconnect(peer.handle);
  }
}

static void GrinderBleCloseOwner(const GrinderBlePeer &owner, const char *reason) {
  GrinderTcp.close_pending = true;
  GrinderTcpRelayOff(reason);
  GrinderBleDisconnect(owner, false);
}

static void GrinderBleReject(const GrinderBlePeer &owner, uint8_t id, GrinderTcpReason reason) {
  GrinderTcp.close_pending = true;
  GrinderTcpRelayOff(GrinderTcpReasonText(reason));
  char response[64];
  GrinderTcpFormatErr(response, sizeof(response), GrinderTcp.plug_mac, reason);
  GrinderBleNotify(owner, false, id, response);
  GrinderBleDisconnect(owner, false);
}

static bool GrinderBleCreateService(const uint8_t (&manufacturer)[8]) {
  auto *server = NimBLEDevice::createServer();
  if (!server) {
    return false;
  }
  server->setCallbacks(&GrinderBleServerEvents, false);
  server->advertiseOnDisconnect(false);
  NimBLEService *service = server->createService(GrinderBleServiceUuid);
  if (!service) {
    return false;
  }
  auto *command = service->createCharacteristic(GrinderBleCommandUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  auto *reply = service->createCharacteristic(GrinderBleReplyUuid, NIMBLE_PROPERTY::NOTIFY);
  if (!command || !reply) {
    return false;
  }
  command->setCallbacks(&GrinderBleCharacteristicEvents);
  reply->setCallbacks(&GrinderBleCharacteristicEvents);
  if (!service->start()) {
    return false;
  }
  NimBLEAdvertisementData data;
  auto *advertising = NimBLEDevice::getAdvertising();
  if (!advertising || !data.setFlags(0x06) || !data.addServiceUUID(GrinderBleServiceUuid) ||
      !data.setManufacturerData(manufacturer, sizeof(manufacturer)) || !advertising->setAdvertisementData(data)) {
    return false;
  }
  advertising->enableScanResponse(false);
  server->start();
  if (!service->getHandle() || !command->getHandle() || !reply->getHandle()) {
    return false;
  }
  GrinderBle.server = server;
  GrinderBle.reply = reply;
  GrinderBle.advertising = advertising;
  return true;
}

static bool GrinderBleConfigure(void) {
  if (GrinderBle.advertising) {
    return true;
  }
  const uint32_t now = millis();
  if (now - GrinderBle.setupAttemptAt < GrinderBleSetupRetry) {
    return false;
  }
  GrinderBle.setupAttemptAt = now;
  if (GrinderBle.setupNeedsReset) {
    if (!NimBLEDevice::deinit(false) || !NimBLEDevice::deinit(true)) {
      AddLog(LOG_LEVEL_INFO, PSTR("GRB: Setup reset failed; will retry"));
      return false;
    }
    GrinderBle.setupNeedsReset = false;
  }
  uint8_t manufacturer[8] = {0xFF, 0xFF};
  if (GrinderTcpCacheIdentity() && ESP_OK == esp_read_mac(manufacturer + 2, ESP_MAC_WIFI_STA) &&
      NimBLEDevice::init("")) {
    if (GrinderBleCreateService(manufacturer)) {
      AddLog(LOG_LEVEL_INFO, PSTR("GRB: Ready %s"), GrinderTcp.plug_mac);
      return true;
    }
    GrinderBle.setupNeedsReset = true;
  }
  AddLog(LOG_LEVEL_INFO, PSTR("GRB: Setup failed; will retry"));
  return false;
}

static void GrinderBleStop(const char *reason) {
  portENTER_CRITICAL(&GrinderBleMux);
  GrinderBleInput.enabled = false;
  const GrinderBlePeer owner = GrinderBleInput.owner;
  const GrinderBlePeer contender = GrinderBleInput.contender;
  portEXIT_CRITICAL(&GrinderBleMux);
  GrinderBle.active = false;
  GrinderTcp.client_open = false;
  GrinderTcp.greeted = false;
  GrinderTcpRelayOff(reason);
  if (GrinderBle.advertising) {
    GrinderBle.advertising->stop();
    GrinderBleDisconnect(owner, false);
    GrinderBleDisconnect(contender, true);
  }
}

static void GrinderBleProcessCommand(const GrinderBlePeer &owner, const GrinderBleCommand &command, uint32_t receivedAt) {
  if (command.parsed.reason != GRINDER_TCP_REASON_NONE) {
    GrinderBleReject(owner, command.id, command.parsed.reason);
    return;
  }
  GrinderTcp.last_rx = receivedAt;
  switch (command.parsed.action) {
    case GRINDER_TCP_ACTION_HELLO:
      GrinderTcp.greeted = true;
      break;
    case GRINDER_TCP_ACTION_ON:
      if (GrinderBleOwnsRelayLink()) {
        GrinderTcpRelayOnCommand();
      }
      break;
    case GRINDER_TCP_ACTION_OFF:
    case GRINDER_TCP_ACTION_BYE:
      GrinderTcpRelayOff();
      break;
    default:
      break;
  }
  char response[48];
  GrinderTcpFormatOk(response, sizeof(response), GrinderTcp.plug_mac, GrinderTcpRelayStateOn());
  if (!GrinderBleNotify(owner, false, command.id, response)) {
    GrinderBleCloseOwner(owner, "notify_failed");
  } else if (command.parsed.action == GRINDER_TCP_ACTION_BYE) {
    GrinderBleCloseOwner(owner, "bye");
  }
}

static void GrinderBlePollOwner(const GrinderBlePeer &owner, GrinderTcpReason fault, uint8_t faultId) {
  if (GrinderBle.ownerGeneration != owner.generation) {
    GrinderTcpRelayOff("ble_owner_changed");
    GrinderBle.ownerGeneration = owner.generation;
    GrinderBle.line = {};
    GrinderTcp.client_open = owner.connected();
    GrinderTcp.greeted = false;
    GrinderTcp.close_pending = owner.closing;
    GrinderTcp.last_rx = owner.connectedAt;
  }
  if (!owner.connected()) {
    return;
  }
  if (GrinderTcp.close_pending || owner.closing) {
    GrinderBleDisconnect(owner, false);
    return;
  }
  if (fault != GRINDER_TCP_REASON_NONE) {
    GrinderBleReject(owner, faultId, fault);
    return;
  }
  for (uint8_t index = 0; index < GrinderBleQueueDepth && !GrinderTcp.close_pending; index++) {
    GrinderBlePacket packet{};
    portENTER_CRITICAL(&GrinderBleMux);
    const bool found = GrinderBleInput.take(owner.generation, packet);
    portEXIT_CRITICAL(&GrinderBleMux);
    const uint32_t now = millis();
    const uint32_t timeout = GrinderTcp.greeted ? GRINDER_TCP_HEARTBEAT_TIMEOUT : GrinderBleHelloTimeout;
    if (now - GrinderTcp.last_rx >= timeout) {
      GrinderBleCloseOwner(owner, GrinderTcp.greeted ? "heartbeat_timeout" : "hello_timeout");
      return;
    }
    if (!found) {
      break;
    }
    const GrinderBleCommand command = GrinderBleRead(GrinderBle.line, packet.data, packet.length, GrinderTcp.greeted);
    if (!command.pending) {
      GrinderBleProcessCommand(owner, command, packet.receivedAt);
    }
  }
}

static void GrinderBlePollContender(const GrinderBlePeer &contender, uint32_t now) {
  if (GrinderBle.contenderGeneration != contender.generation) {
    GrinderBle.contenderGeneration = contender.generation;
    GrinderBle.contenderReplied = false;
  }
  if (!contender.connected()) {
    return;
  }
  const bool expired = GrinderBle.contenderReplied
    ? now - GrinderBle.contenderRepliedAt >= GrinderBleBusyGrace
    : now - contender.connectedAt >= GrinderBleBusyTimeout;
  if (contender.closing || expired) {
    GrinderBleDisconnect(contender, true);
    return;
  }
  if (contender.subscribed && !GrinderBle.contenderReplied) {
    char response[32];
    GrinderTcpFormatBusy(response, sizeof(response), GrinderTcp.plug_mac);
    if (!GrinderBleNotify(contender, true, 0, response)) {
      GrinderBleDisconnect(contender, true);
      return;
    }
    GrinderBle.contenderReplied = true;
    GrinderBle.contenderRepliedAt = now;
  }
}

static void GrinderBleLoop(void) {
  if (GrinderTcp.suspended || TasmotaGlobal.restart_flag || TasmotaGlobal.ota_state_flag ||
      !GrinderTcpRelayHardwareSupported()) {
    if (GrinderBle.active || GrinderTcpRelayStateOn()) {
      GrinderBleStop("maintenance_or_layout");
    }
    return;
  }
  if (!GrinderBle.active) {
    if (!GrinderBleConfigure()) {
      return;
    }
    portENTER_CRITICAL(&GrinderBleMux);
    GrinderBleInput.enabled = true;
    portEXIT_CRITICAL(&GrinderBleMux);
    GrinderBle.active = true;
  }
  portENTER_CRITICAL(&GrinderBleMux);
  const GrinderBlePeer owner = GrinderBleInput.owner;
  const GrinderBlePeer contender = GrinderBleInput.contender;
  const GrinderTcpReason fault = GrinderBleInput.fault;
  const uint8_t faultId = GrinderBleInput.faultId;
  portEXIT_CRITICAL(&GrinderBleMux);
  const uint32_t now = millis();
  GrinderBlePollOwner(owner, fault, faultId);
  GrinderTcpEnforceRelayOwnership();
  if ((owner.connected() || contender.connected()) && TasmotaGlobal.skip_sleep < 1) {
    TasmotaGlobal.skip_sleep = 1;
  }
  GrinderBlePollContender(contender, now);
  if ((!owner.connected() || !contender.connected()) && !GrinderBle.advertising->isAdvertising() &&
      now - GrinderBle.advertisingAttemptAt >= GrinderBleAdvertisingRetry) {
    GrinderBle.advertisingAttemptAt = now;
    GrinderBle.advertising->start();
  }
}
