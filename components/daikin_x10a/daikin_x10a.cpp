#include "daikin_x10a.h"
#include "register_definitions.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "daikin_package.h"

#include <vector>
#include <string>
#include <set>
#include <cstdlib>  // for atof
#include <cmath>    // for NAN, std::isnan

namespace esphome {
namespace daikin_x10a {

DaikinX10A::~DaikinX10A() = default;

static constexpr uint32_t Serial_TimeoutInMilliseconds = 300;

//__________________________________________________________________________________________________________________________ loop begin
void DaikinX10A::loop() {
  static uint32_t start = 0;
  static bool initialDelayDone = false;

  if (!initialDelayDone) {
    if (millis() >= 15000) {
      start = millis();
      initialDelayDone = true;
    }
    return;
  }

  if (millis() - start >= REGISTER_SCAN_INTERVAL_MS) {
    start = millis();
    this->FetchRegisters();
  }
}
//________________________________________________________________ loop end

//__________________________________________________________________________________________________________________________ FetchRegisters begin
// FetchRegisters() is called every REGISTER_SCAN_INTERVAL_MS milliseconds, and loops over all registers with Mode==1 (read) to fetch their values from the HP via UART
void DaikinX10A::FetchRegisters() {
  std::set<uint8_t> fetched_registries;  // Track which registryIDs we've already requested in this scan cycle

  for (const auto& selectedRegister : registers_) {  //____________________________________ loop over all registers
    if (selectedRegister.Mode >= 1) {
      // Skip if we've already fetched this registryID in this scan cycle
      if (fetched_registries.count(selectedRegister.registryID) > 0) {
        continue;
      }

      // Mark this registryID as fetched
      fetched_registries.insert(selectedRegister.registryID);

      auto MyDaikinRequestPackage = daikin_package::MakeRequest(selectedRegister.registryID);

      if (debug_mode_) ESP_LOGI("ESPoeDaikin", "TX (%u): %s", (unsigned)MyDaikinRequestPackage.size(), MyDaikinRequestPackage.ToHexString().c_str());

      this->flush();  // Clear the serial buffer before sending
      for (uint8_t requestByte : MyDaikinRequestPackage.buffer()) this->write(requestByte);

      daikin_package MyDaikinPackage(daikin_package::Mode::RECEIVE);

      const uint32_t FetchRegistersStartMilliseconds = millis();
      size_t IncommingPackageSize = 3;
      uint8_t target_registry = selectedRegister.registryID;
      
      // Retry loop: keep reading until we get the CORRECT registry response or timeout
      while (millis() - FetchRegistersStartMilliseconds < Serial_TimeoutInMilliseconds) {
        if (!this->available()) continue;
        
        uint8_t incomingByte;
        this->read_byte(&incomingByte);

        // Skip if not protocol marker (0x40)
        if (MyDaikinPackage.empty() && incomingByte != 0x40) continue;

        MyDaikinPackage.buffer_mut().push_back(incomingByte);

        // Once we have 2 bytes, check if it's the right registry
        if (MyDaikinPackage.size() == 2) {
          uint8_t received_registry = MyDaikinPackage.buffer()[1];
          if (received_registry != target_registry) {
            // Wrong registry - discard this packet and wait for the right one
            if (debug_mode_) ESP_LOGI("ESPoeDaikin", "  Received registry 0x%02X (expected 0x%02X), discarding...", received_registry, target_registry);
            MyDaikinPackage.clear();
            continue;
          }
        }

        // Once we have 3 bytes, we know the full length
        if (MyDaikinPackage.HasMinimalHeader()) {
          const size_t expectedSize = MyDaikinPackage.expected_size();
          if (expectedSize > 0) IncommingPackageSize = expectedSize;
        }

        // Early error detection
        if (MyDaikinPackage.is_error_frame()) {
          if (debug_mode_) ESP_LOGI("ESPoeDaikin", "HP returned error frame: %s", MyDaikinPackage.ToHexString().c_str());
          return;
        }

        // Check if we have complete packet
        if (MyDaikinPackage.size() >= IncommingPackageSize) break;
      }

      if (MyDaikinPackage.empty()) {
        if (debug_mode_) ESP_LOGI("ESPoeDaikin", "No valid response for registry 0x%02X (timeout)", target_registry);
        continue;
      }

      if (!MyDaikinPackage.Valid_CRC()) {
        if (debug_mode_) ESP_LOGI("ESPoeDaikin", "CRC mismatch (%u): %s", (unsigned)MyDaikinPackage.size(), MyDaikinPackage.ToHexString().c_str());
        continue;
      }

      if (debug_mode_) ESP_LOGI("ESPoeDaikin", "Register 0x%02X (%u bytes): %s", target_registry, (unsigned)MyDaikinPackage.size(), MyDaikinPackage.ToHexString().c_str());
      last_requested_registry_ = selectedRegister.registryID;
      this->process_frame_(MyDaikinPackage);
    } // if mode==1

  } //____________________________________ end for loop loop over all registers
  
}
//________________________________________________________________ FetchRegisters end

//__________________________________________________________________________________________________________________________ process_frame_ begin
void DaikinX10A::process_frame_(daikin_package &pkg) {
  if (!pkg.is_valid_protocol() || !pkg.Valid_CRC() || pkg.is_error_frame()) return;

  const auto &buffer = pkg.buffer();
  if (buffer.size() < 6) return;

  // Registry ID is at byte 1
  uint8_t registry_id = buffer[1];

  if (debug_mode_) ESP_LOGI("ESPoeDaikin", "Decode registry_id=%d (0x%02X), protocol_header=3_bytes (0x40, regID, length), data_starts_at_byte_3", (int)registry_id, registry_id);

  convert_registry_values_(pkg, registry_id);

  // log alle regels die bij deze registry horen (en non-empty asString hebben)
  int count = 0;
  for (auto &registerEntry : registers_) {
    if ((uint8_t)registerEntry.registryID != registry_id) continue;
    if (registerEntry.asString[0] == '\0') continue;
    if (debug_mode_) ESP_LOGI("ESPoeDaikin", "0x%02X | %s = %s", registry_id, registerEntry.label, registerEntry.asString);

    // AUTO-UPDATE DYNAMIC SENSORS for mode=1 registers
    // Try text sensor first, then numeric sensor (based on ConversionID, the right one will exist)
    if (registerEntry.Mode == 1) {
      if (!update_text_sensor(registerEntry.label, std::string(registerEntry.asString))) {
        float value = std::atof(registerEntry.asString);
        update_sensor(registerEntry.label, value);
      }
    }

    count++;
  }

  if (debug_mode_) ESP_LOGI("ESPoeDaikin", "Decoded %d values for registry 0x%02X", count, registry_id);
}
//________________________________________________________________ process_frame_ end

//__________________________________________________________________________________________________________________________ add_register begin
void DaikinX10A::add_register(int mode, int ConversionID, int offset, int registryID,
                             int dataSize, int dataType, const char* label) {
  registers_.emplace_back(mode, ConversionID, offset, registryID, dataSize, dataType, label);
}
//________________________________________________________________ add_register end

//__________________________________________________________________________________________________________________________ get_register_value begin
std::string DaikinX10A::get_register_value(const std::string& label) const {
  for (const auto &reg : registers_) {
    if (reg.label && reg.label == label && reg.asString[0] != '\0') {
      return std::string(reg.asString);
    }
  }
  return "";  // Return empty string if register not found or value is empty
}
//________________________________________________________________ get_register_value end

//__________________________________________________________________________________________________________________________ register_dynamic_sensor begin
void DaikinX10A::register_dynamic_sensor(const std::string& label, sensor::Sensor *sens) {
  dynamic_sensors_[label] = sens;
  ESP_LOGI("ESPoeDaikin", "Registered dynamic sensor: %s", label.c_str());
}
//________________________________________________________________ register_dynamic_sensor end

//__________________________________________________________________________________________________________________________ update_sensor begin
void DaikinX10A::update_sensor(const std::string& label, float value) {
  auto it = dynamic_sensors_.find(label);
  if (it != dynamic_sensors_.end() && it->second != nullptr) {
    it->second->publish_state(value);
    ESP_LOGV("ESPoeDaikin", "Updated sensor '%s' = %.1f", label.c_str(), value);
  }
}
//________________________________________________________________ update_sensor end

void DaikinX10A::register_dynamic_text_sensor(const std::string& label, text_sensor::TextSensor *sens) {
  dynamic_text_sensors_[label] = sens;
  ESP_LOGI("ESPoeDaikin", "Registered dynamic text sensor: %s", label.c_str());
}

bool DaikinX10A::update_text_sensor(const std::string& label, const std::string& value) {
  auto it = dynamic_text_sensors_.find(label);
  if (it != dynamic_text_sensors_.end() && it->second != nullptr) {
    it->second->publish_state(value);
    ESP_LOGV("ESPoeDaikin", "Updated text sensor '%s' = %s", label.c_str(), value.c_str());
    return true;
  }
  return false;
}

//__________________________________________________________________________________________________________________________ convert_registry_values_ begin
void DaikinX10A::convert_registry_values_(const daikin_package &pkg, uint8_t reg_id) {
  unsigned offset = pkg.data_offset();

  if (debug_mode_) ESP_LOGI("ESPoeDaikin", "convert_registry_values: registry_id = %d (hex: 0x%02X), data_offset = %u", (int)reg_id, reg_id, offset);
  for (auto &reg : registers_) {
    if (static_cast<uint8_t>(reg.registryID) != reg_id) continue;

    const unsigned int idx  = static_cast<unsigned int>(reg.offset) + offset;
    const unsigned int need = idx + static_cast<unsigned int>(reg.dataSize);
    if (need > pkg.size()) continue;

    const uint8_t *input = &pkg.buffer()[idx];
    convert_one_(reg, input);
  }
}
//________________________________________________________________ convert_registry_values_ end

//__________________________________________________________________________________________________________________________ numeric helpers begin
unsigned short DaikinX10A::getUnsignedValue_(const uint8_t *data, int dataSize, int cnvflg) {
  if (dataSize == 1) return (unsigned short)data[0];
  if (cnvflg == 0) return (unsigned short)((data[1] << 8) | data[0]);
  return (unsigned short)((data[0] << 8) | data[1]);
}

short DaikinX10A::getSignedValue_(const uint8_t *data, int datasize, int cnvflg) {
  unsigned short num = getUnsignedValue_(data, datasize, cnvflg);
  short result = (short)num;
  if ((num & 0x8000) != 0) {
    num = (unsigned short)~num;
    num += 1;
    result = (short)((int)num * -1);
  }
  return result;
}

double DaikinX10A::convertPress2Temp_(double data) { // R32
  double num  = -2.6989493795556E-07 * data * data * data * data * data * data;
  double num2 =  4.26383417104661E-05 * data * data * data * data * data;
  double num3 = -0.00262978346547749 * data * data * data * data;
  double num4 =  0.0805858127503585  * data * data * data;
  double num5 = -1.31924457284073    * data * data;
  double num6 =  13.4157368435437    * data;
  double num7 = -51.1813342993155;
  return num + num2 + num3 + num4 + num5 + num6 + num7;
}
//________________________________________________________________ numeric helpers end

//__________________________________________________________________________________________________________________________ table converters begin
void DaikinX10A::convertTable200_(const uint8_t *data, char *ret) {
  strcpy(ret, (data[0] == 0) ? "OFF" : "ON");
}

void DaikinX10A::convertTable203_(const uint8_t *data, char *ret) {
  switch (data[0]) {
    case 0: strcpy(ret, "Normal"); break;
    case 1: strcpy(ret, "Error"); break;
    case 2: strcpy(ret, "Warning"); break;
    case 3: strcpy(ret, "Caution"); break;
    default: strcpy(ret, "-"); break;
  }
}

void DaikinX10A::convertTable204_(const uint8_t *data, char *ret) {
  const char array[]  = " ACEHFJLPU987654";
  const char array2[] = "0123456789AHCJEF";
  int n1 = (data[0] >> 4) & 15;
  int n2 = (int)(data[0] & 15);
  ret[0] = array[n1];
  ret[1] = array2[n2];
  ret[2] = 0;
}

double DaikinX10A::convertTable312_(const uint8_t *data) {
  double dbl = (((uint8_t)(7 & (data[0] >> 4))) + (uint8_t)(15U & data[0])) / 16.0;
  if ((data[0] & 0x80) > 0) dbl *= -1.0;
  return dbl;
}

void DaikinX10A::convertTable315_(const uint8_t *data, char *ret) {
  uint8_t b = (data[0] & 0xF0) >> 4;
  switch (b) {
    case 0: strcpy(ret, "Stop"); break;
    case 1: strcpy(ret, "Heating"); break;
    case 2: strcpy(ret, "Cooling"); break;
    case 4: strcpy(ret, "DHW"); break;
    case 5: strcpy(ret, "Heating + DHW"); break;
    case 6: strcpy(ret, "Cooling + DHW"); break;
    default: strcpy(ret, "-"); break;
  }
}

void DaikinX10A::convertTable316_(const uint8_t *data, char *ret) {
  uint8_t b = (data[0] & 0xF0) >> 4;
  switch (b) {
    case 0: strcpy(ret, "H/P only"); break;
    case 1: strcpy(ret, "Hybrid"); break;
    case 2: strcpy(ret, "Boiler only"); break;
    default: strcpy(ret, "Unknown"); break;
  }
}

void DaikinX10A::convertTable217_(const uint8_t *data, char *ret) {
  const char* r217[] = {
    "Fan Only","Heating","Cooling","Auto","Ventilation","Auto Cool","Auto Heat","Dry","Aux.",
    "Cooling Storage","Heating Storage",
    "UseStrdThrm(cl)1","UseStrdThrm(cl)2","UseStrdThrm(cl)3","UseStrdThrm(cl)4",
    "UseStrdThrm(ht)1","UseStrdThrm(ht)2","UseStrdThrm(ht)3","UseStrdThrm(ht)4"
  };
  int idx = (int)data[0];
  if (idx >= 0 && idx < (int)(sizeof(r217) / sizeof(r217[0]))) strcpy(ret, r217[idx]);
  else strcpy(ret, "-");
}

void DaikinX10A::convertTable300_(const uint8_t *data, int tableID, char *ret) {
  uint8_t mask = (uint8_t)(1U << (tableID % 10));
  strcpy(ret, ((data[0] & mask) != 0) ? "ON" : "OFF");
}
//________________________________________________________________ table converters end

//__________________________________________________________________________________________________________________________ convert_one_ begin
void DaikinX10A::convert_one_(Register &def, const uint8_t *data) {
  def.asString[0] = '\0';

  const int ConversionID = def.ConversionID;
  const int num = def.dataSize;
  double dblData = NAN;

  switch (ConversionID) {
    case 0x00:
      return;

    case 100:
      strncat(def.asString, (const char*)data, (size_t)num);
      return;

    // signed
    case 101: dblData = (double)getSignedValue_(data, num, 0); break;
    case 102: dblData = (double)getSignedValue_(data, num, 1); break;
    case 103: dblData = (double)getSignedValue_(data, num, 0) / 256.0; break;
    case 104: dblData = (double)getSignedValue_(data, num, 1) / 256.0; break;
    case 105:
      dblData = (double)getSignedValue_(data, num, 0) * 0.1;
      ESP_LOGV("ESPoeDaikin", "    ConversionID 105: bytes[0]=0x%02X bytes[1]=0x%02X → %g°C", data[0], (num > 1 ? data[1] : 0), dblData);
      break;
    case 106: dblData = (double)getSignedValue_(data, num, 1) * 0.1; break;

    case 107:
      dblData = (double)getSignedValue_(data, num, 0) * 0.1;
      if (dblData == -3276.8) { strcpy(def.asString, "---"); return; }
      break;

    case 108:
      dblData = (double)getSignedValue_(data, num, 1) * 0.1;
      if (dblData == -3276.8) { strcpy(def.asString, "---"); return; }
      break;
    case 109: dblData = (double)getSignedValue_(data, num, 0) / 256.0 * 2.0; break;
    case 110: dblData = (double)getSignedValue_(data, num, 1) / 256.0 * 2.0; break;
    case 111: dblData = (double)getSignedValue_(data, num, 1) * 0.5; break;
    case 112: dblData = (double)(getSignedValue_(data, num, 1) - 64) * 0.5; break;
    case 113: dblData = (double)getSignedValue_(data, num, 1) * 0.25; break;
    case 114: {
      if (data[0] == 0 && data[1] == 128) { strcpy(def.asString, "---"); return; }
      unsigned short v = (unsigned short)((int)data[1] * 256);
      v |= (unsigned short)data[0];
      bool neg = (data[1] & 128) != 0;
      if (neg) v = ~(v - 1);
      dblData = (double)((v & 0xFF00) / 256) + (double)(v & 0xFF) / 256.0;
      dblData *= 10.0;
      if (neg) dblData *= -1.0;
      break;
    }
    case 115: dblData = (double)getSignedValue_(data, num, 0) / 2560.0; break;
    case 116: dblData = (double)getSignedValue_(data, num, 1) / 2560.0; break;
    case 117: dblData = (double)getSignedValue_(data, num, 0) * 0.01; break;
    case 118: dblData = (double)getSignedValue_(data, num, 1) * 0.01; break;
    case 119: {
      if (data[0] == 0 && data[1] == 128) { strcpy(def.asString, "---"); return; }
      unsigned short v = (unsigned short)((int)data[1] * 256);
      v |= (unsigned short)(data[0] & 127);
      dblData = (double)((v & 0xFF00) / 256) + (double)(v & 0xFF) / 256.0;
      break;
    }

    // unsigned
    case 151: dblData = (double)getUnsignedValue_(data, num, 0); break;
    case 152: dblData = (double)getUnsignedValue_(data, num, 1); break;
    case 153: dblData = (double)getUnsignedValue_(data, num, 0) / 256.0; break;
    case 154: dblData = (double)getUnsignedValue_(data, num, 1) / 256.0; break;
    case 155: dblData = (double)getUnsignedValue_(data, num, 0) * 0.1; break;
    case 156: dblData = (double)getUnsignedValue_(data, num, 1) * 0.1; break;
    case 157: dblData = (double)getUnsignedValue_(data, num, 0) / 256.0 * 2.0; break;
    case 158: dblData = (double)getUnsignedValue_(data, num, 1) / 256.0 * 2.0; break;
    case 161: dblData = (double)getUnsignedValue_(data, num, 1) * 0.5; break;
    case 162: dblData = (double)(getUnsignedValue_(data, num, 1) - 64) * 0.5; break;
    case 163: dblData = (double)getUnsignedValue_(data, num, 1) * 0.25; break;
    case 164: dblData = (double)getUnsignedValue_(data, num, 1) * 5; break;
    case 165: dblData = (double)(getUnsignedValue_(data, num, 0) & 16383); break;

    // tables
    case 200: convertTable200_(data, def.asString); return;
    case 203: convertTable203_(data, def.asString); return;
    case 204: convertTable204_(data, def.asString); return;
    case 201:
    case 217: convertTable217_(data, def.asString); return;

    case 215:
    case 216: {
      int hi = data[0] >> 4;
      int lo = data[0] & 0x0F;
      snprintf(def.asString, sizeof(def.asString), "%d%d", hi, lo);
      return;
    }

    case 300: case 301: case 302: case 303: case 304: case 305: case 306: case 307:
      convertTable300_(data, def.ConversionID, def.asString);
      return;

    case 312:
      dblData = convertTable312_(data);
      break;

    case 315: convertTable315_(data, def.asString); return;
    case 316: convertTable316_(data, def.asString); return;

    case 211:
      if (data[0] == 0) { strcpy(def.asString, "OFF"); return; }
      dblData = (double)data[0];
      break;

    // pressure -> temp
    case 401: dblData = convertPress2Temp_((double)getSignedValue_(data, num, 0)); break;
    case 402: dblData = convertPress2Temp_((double)getSignedValue_(data, num, 1)); break;
    case 403: dblData = convertPress2Temp_((double)getSignedValue_(data, num, 0) / 256.0); break;
    case 404: dblData = convertPress2Temp_((double)getSignedValue_(data, num, 1) / 256.0); break;
    case 405: dblData = convertPress2Temp_((double)getSignedValue_(data, num, 0) * 0.1); break;
    case 406: dblData = convertPress2Temp_((double)getSignedValue_(data, num, 1) * 0.1); break;

    default:
      snprintf(def.asString, sizeof(def.asString), "Conv %d NA", ConversionID);
      return;
  }

  if (!std::isnan(dblData)) {
    snprintf(def.asString, sizeof(def.asString), "%g", dblData);
  }
}
//________________________________________________________________ convert_one_ end

}  // namespace daikin_x10a
}  // namespace esphome
