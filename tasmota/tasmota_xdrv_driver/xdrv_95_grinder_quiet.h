#pragma once

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
