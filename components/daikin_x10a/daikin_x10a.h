#pragma once
#include "esphome/core/component.h"
#include "esphome/core/application.h"      // App
#include "esphome/core/entity_base.h"      // EntityCategory
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <vector>
#include <string>
#include <map>
#include "daikin_package.h"
#include "register_definitions.h"

namespace esphome {
namespace daikin_x10a {

class DaikinX10A : public uart::UARTDevice, public Component {
 public:
  explicit DaikinX10A(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}
  virtual ~DaikinX10A();
    void loop() override;
    void FetchRegisters();
    void add_register(int mode, int ConversionID, int offset, int registryID, int dataSize, int dataType, const char* label);

    // Get register value by label name (for template sensors)
    std::string get_register_value(const std::string& label) const;

    // Dynamic sensor management
    void register_dynamic_sensor(const std::string& label, sensor::Sensor *sens);
    void update_sensor(const std::string& label, float value);

    // Dynamic text sensor management
    void register_dynamic_text_sensor(const std::string& label, text_sensor::TextSensor *sens);
    bool update_text_sensor(const std::string& label, const std::string& value);

    // Debug mode
    void set_debug_mode(bool enabled) { debug_mode_ = enabled; }
    bool get_debug_mode() const { return debug_mode_; }

 protected:
  bool debug_mode_{false};
  std::vector<uint8_t> buffer_;
  uint8_t last_requested_registry_{0};
  std::vector<Register> registers_;

  // Map of label -> sensor for dynamic sensors
  std::map<std::string, sensor::Sensor*> dynamic_sensors_;
  std::map<std::string, text_sensor::TextSensor*> dynamic_text_sensors_;

  void process_frame_(daikin_package &pkg);

  // Conversion logic (moved from daikin_package)
  void convert_registry_values_(const daikin_package &pkg, uint8_t reg_id);
  static void convert_one_(Register &def, const uint8_t *data);

  // Numeric helpers
  static unsigned short getUnsignedValue_(const uint8_t *data, int dataSize, int cnvflg);
  static short getSignedValue_(const uint8_t *data, int datasize, int cnvflg);
  static double convertPress2Temp_(double data);

  // Table converters
  static void convertTable200_(const uint8_t *data, char *ret);
  static void convertTable203_(const uint8_t *data, char *ret);
  static void convertTable204_(const uint8_t *data, char *ret);
  static double convertTable312_(const uint8_t *data);
  static void convertTable315_(const uint8_t *data, char *ret);
  static void convertTable316_(const uint8_t *data, char *ret);
  static void convertTable217_(const uint8_t *data, char *ret);
  static void convertTable300_(const uint8_t *data, int tableID, char *ret);
};

}  // namespace daikin_x10a
}  // namespace esphome
