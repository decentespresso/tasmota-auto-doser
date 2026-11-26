#pragma once

#include "xdrv_95_grinder_ble_protocol.h"

constexpr uint16_t GrinderBleNoPeer = UINT16_MAX;
constexpr uint8_t GrinderBleQueueDepth = 8;

struct GrinderBlePeer {
  uint16_t handle = GrinderBleNoPeer;
  uint32_t generation = 0;
  uint32_t connectedAt = 0;
  bool subscribed = false;
  bool closing = false;

  bool connected(void) const { return handle != GrinderBleNoPeer; }
};

struct GrinderBlePacket {
  uint8_t length;
  uint8_t data[GrinderBleValueSize];
};

struct GrinderBleMailbox {
  GrinderBlePeer owner;
  GrinderBlePeer contender;
  GrinderBlePacket packets[GrinderBleQueueDepth]{};
  uint32_t generation = 0;
  uint8_t head = 0;
  uint8_t count = 0;
  uint8_t faultId = 0;
  GrinderTcpReason fault = GRINDER_TCP_REASON_NONE;
  bool enabled = false;
  bool urgent = false;

  bool connect(uint16_t handle, uint32_t now) {
    if (!enabled || (owner.connected() && contender.connected())) {
      return false;
    }
    GrinderBlePeer &peer = owner.connected() ? contender : owner;
    peer = {handle, ++generation, now};
    return true;
  }

  void disconnect(uint16_t handle) {
    if (owner.handle == handle) {
      owner = {};
      clearInput();
    } else if (contender.handle == handle) {
      contender = {};
    }
  }

  void subscribe(uint16_t handle, bool enabled) {
    if (owner.handle == handle) {
      if (owner.subscribed && !enabled) {
        fail(0, GRINDER_TCP_REASON_INVALID_CHAR);
      }
      owner.subscribed = enabled;
    } else if (contender.handle == handle) {
      contender.subscribed = enabled;
    }
  }

  void write(uint16_t handle, const uint8_t *data, size_t length) {
    if (owner.handle != handle || owner.closing || fault != GRINDER_TCP_REASON_NONE) {
      return;
    }
    const uint8_t id = length ? data[0] : 0;
    if (!owner.subscribed || !id || length < 2 || length > GrinderBleValueSize) {
      fail(id, GRINDER_TCP_REASON_INVALID_CHAR);
      return;
    }
    if (GrinderBleIsFastOff(data, length)) {
      head = count = 0;
      urgent = true;
    }
    if (count == GrinderBleQueueDepth) {
      fail(id, GRINDER_TCP_REASON_LINE_OVERFLOW);
      return;
    }
    GrinderBlePacket &packet = packets[(head + count) % GrinderBleQueueDepth];
    packet.length = static_cast<uint8_t>(length);
    memcpy(packet.data, data, length);
    count++;
  }

  bool take(uint32_t generation, GrinderBlePacket &packet) {
    if (!owner.connected() || owner.generation != generation || owner.closing ||
        fault != GRINDER_TCP_REASON_NONE || !count) {
      return false;
    }
    packet = packets[head];
    if (GrinderBleIsFastOff(packet.data, packet.length)) {
      urgent = false;
    }
    head = (head + 1) % GrinderBleQueueDepth;
    count--;
    return true;
  }

  bool owns(uint32_t generation) const {
    return enabled && owner.connected() && owner.generation == generation && owner.subscribed &&
      !owner.closing && !urgent && fault == GRINDER_TCP_REASON_NONE;
  }

  void close(GrinderBlePeer &peer, uint32_t expected) {
    if (peer.connected() && peer.generation == expected) {
      peer.closing = true;
    }
  }

 private:
  void clearInput(void) {
    count = head = faultId = 0;
    fault = GRINDER_TCP_REASON_NONE;
    urgent = false;
  }

  void fail(uint8_t id, GrinderTcpReason reason) {
    count = head = 0;
    faultId = id;
    fault = reason;
  }
};
