// daikin_package.h
#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

class daikin_package {
 public:
  enum class Mode { SEND_REQUEST, RECEIVE };

  daikin_package() = default;
  explicit daikin_package(Mode m) : mode_(m) {}

  // --- Factory helpers ---
  static daikin_package MakeRequest(uint8_t reg_id) {
    daikin_package MyNewPackage(Mode::SEND_REQUEST);
    MyNewPackage.packet_buffer = {0x03, 0x40, reg_id, 0x00};
    MyNewPackage.packet_buffer[3] = crc_(MyNewPackage.packet_buffer.data(), 3);
    return MyNewPackage;
  }

  static daikin_package FromBytes(const std::vector<uint8_t>& bytes) {
    daikin_package MyNewPackage(Mode::RECEIVE);
    MyNewPackage.packet_buffer = bytes;
    return MyNewPackage;
  }

  // --- Buffer access ---
  const std::vector<uint8_t>& buffer() const { return packet_buffer; }
  std::vector<uint8_t>& buffer_mut() { return packet_buffer; }
  void clear() { packet_buffer.clear(); }

  bool empty() const { return packet_buffer.empty(); }
  size_t size() const { return packet_buffer.size(); }

  bool HasMinimalHeader() const { return packet_buffer.size() >= 3; }
  size_t expected_size() const {
    if (!HasMinimalHeader()) return 0;
    return static_cast<size_t>(packet_buffer[2]) + 2;
  }

  bool is_error_frame() const {
    return packet_buffer.size() >= 2 && packet_buffer[0] == 0x15 && packet_buffer[1] == 0xEA;
  }

  bool is_valid_protocol() const { return !packet_buffer.empty() && packet_buffer[0] == 0x40; }

  bool Valid_CRC() const {
    if (packet_buffer.size() < 2) return false;
    const int n = static_cast<int>(packet_buffer.size());
    return crc_(packet_buffer.data(), n - 1) == packet_buffer[n - 1];
  }

  std::string ToHexString() const {
    char b[4];
    std::string out;
    for (uint8_t c : packet_buffer) {
      snprintf(b, sizeof(b), "%02X", c);
      out += b;
      out += ' ';
    }
    return out;
  }

  Mode mode() const { return mode_; }

  // Protocol I: registry ID at byte position 1
  uint8_t registry_id() const {
    return (packet_buffer.size() > 1) ? packet_buffer[1] : 0;
  }

  // Protocol I: data starts at byte position 3 (after: 0x40, registry_id, length)
  unsigned data_offset() const {
    return 3;
  }

 private:
  std::vector<uint8_t> packet_buffer;
  Mode mode_{Mode::RECEIVE};

  // --- CRC ---
  static uint8_t crc_(const uint8_t *src, int len) {
    uint8_t b = 0;
    for (int i = 0; i < len; i++) b += src[i];
    return static_cast<uint8_t>(~b);
  }
};
