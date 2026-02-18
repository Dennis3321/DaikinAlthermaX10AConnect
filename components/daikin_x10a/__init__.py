import re
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, text_sensor
from esphome.const import (
    CONF_ID, CONF_NAME, CONF_ICON, UNIT_CELSIUS,
    CONF_UNIT_OF_MEASUREMENT, CONF_DEVICE_CLASS,
    CONF_STATE_CLASS, CONF_ACCURACY_DECIMALS,
    CONF_DISABLED_BY_DEFAULT, CONF_FORCE_UPDATE,
)
from esphome.core import ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor"]
CODEOWNERS = ["@local"]

CONF_UART_ID = "uart_id"
CONF_REGISTERS = "registers"

# ConversionIDs that produce text output (based on convert_one_ in daikin_x10a.cpp)
TEXT_CONVERSION_IDS = {200, 201, 203, 204, 211, 217, 300, 301, 302, 303, 304, 305, 306, 307, 315, 316}

# ConversionIDs that typically produce temperature values (×0.1, signed temp, etc.)
TEMPERATURE_CONVERSION_IDS = {105, 107, 114, 118, 119, 405}

REGISTER_SCHEMA = cv.Schema({
    cv.Required("mode"): cv.int_,
    cv.Required("ConversionID"): cv.hex_int,
    cv.Required("offset"): cv.int_,
    cv.Required("registryID"): cv.int_,
    cv.Required("dataSize"): cv.int_,
    cv.Required("dataType"): cv.int_,
    cv.Required("label"): cv.string,
    cv.Optional("unit"): cv.string,
    cv.Optional("device_class"): cv.string,
    cv.Optional("accuracy_decimals"): cv.int_,
    cv.Optional("icon"): cv.string,
})

daikin_x10a_ns = cg.esphome_ns.namespace("daikin_x10a")
DaikinX10A = daikin_x10a_ns.class_("DaikinX10A", cg.Component, uart.UARTDevice)

# Expose the method so template sensors can use it
DaikinX10A.add_method(
    "get_register_value",
    cg.std_string,
    [cg.std_string],
)

# Add method to register dynamic sensors
DaikinX10A.add_method(
    "register_dynamic_sensor",
    cg.void,
    [cg.std_string, sensor.Sensor.operator("ptr")],
)

# Add method to update dynamic sensors
DaikinX10A.add_method(
    "update_sensor",
    cg.void,
    [cg.std_string, cg.float_],
)

# Add method to register dynamic text sensors
DaikinX10A.add_method(
    "register_dynamic_text_sensor",
    cg.void,
    [cg.std_string, text_sensor.TextSensor.operator("ptr")],
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DaikinX10A),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Required("mode"): cv.int_,
        cv.Optional(CONF_REGISTERS): cv.ensure_list(REGISTER_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID], uart_comp)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_REGISTERS in config:
        for idx, r in enumerate(config[CONF_REGISTERS]):
            # Add register to component
            cg.add(
                var.add_register(
                    r["mode"],
                    r["ConversionID"],
                    r["offset"],
                    r["registryID"],
                    r["dataSize"],
                    r["dataType"],
                    r["label"],
                )
            )

            # AUTO-CREATE SENSOR for mode=1 registers
            if r["mode"] == 1:
                # Sanitize label for C++ identifier: keep only alphanumeric and underscore
                label_sanitized = re.sub(r'[^a-z0-9]+', '_', r["label"].lower()).strip('_')

                is_text = r["ConversionID"] in TEXT_CONVERSION_IDS

                if is_text:
                    # Text sensor for text-producing ConversionIDs (217, 307, 200, etc.)
                    ts_id = ID(f"daikin_{label_sanitized}", is_declaration=True, type=text_sensor.TextSensor)
                    ts = cg.new_Pvariable(ts_id)
                    ts_config = {
                        CONF_ID: ts_id,
                        CONF_NAME: r["label"],
                        CONF_DISABLED_BY_DEFAULT: False,
                    }
                    if "icon" in r:
                        ts_config[CONF_ICON] = r["icon"]
                    await text_sensor.register_text_sensor(ts, ts_config)
                    cg.add(var.register_dynamic_text_sensor(r["label"], ts))
                else:
                    # Numeric sensor for numeric ConversionIDs (105, 151, etc.)
                    sensor_id = ID(f"daikin_{label_sanitized}", is_declaration=True, type=sensor.Sensor)
                    sens = cg.new_Pvariable(sensor_id)

                    # Determine unit and device_class:
                    # 1. Use explicit values from YAML if provided
                    # 2. Fall back to auto-detect based on ConversionID
                    # 3. Default to no unit/device_class for non-temperature data
                    if "unit" in r:
                        unit = r["unit"]
                    elif r["ConversionID"] in TEMPERATURE_CONVERSION_IDS and r["dataType"] == 1:
                        unit = UNIT_CELSIUS
                    elif r["dataType"] == 2:
                        unit = "bar"
                    elif r["dataType"] == 3:
                        unit = "A"
                    else:
                        unit = ""

                    if "device_class" in r:
                        dev_class = r["device_class"]
                    elif r["ConversionID"] in TEMPERATURE_CONVERSION_IDS and r["dataType"] == 1:
                        dev_class = "temperature"
                    elif r["dataType"] == 2:
                        dev_class = "pressure"
                    elif r["dataType"] == 3:
                        dev_class = "current"
                    else:
                        dev_class = ""

                    accuracy = r.get("accuracy_decimals", 1)

                    sensor_config = {
                        CONF_ID: sensor_id,
                        CONF_NAME: r["label"],
                        CONF_STATE_CLASS: sensor.StateClasses.STATE_CLASS_MEASUREMENT,
                        CONF_ACCURACY_DECIMALS: accuracy,
                        CONF_DISABLED_BY_DEFAULT: False,
                        CONF_FORCE_UPDATE: False,
                    }
                    if unit:
                        sensor_config[CONF_UNIT_OF_MEASUREMENT] = unit
                    if dev_class:
                        sensor_config[CONF_DEVICE_CLASS] = dev_class
                    if "icon" in r:
                        sensor_config[CONF_ICON] = r["icon"]

                    await sensor.register_sensor(sens, sensor_config)
                    cg.add(var.register_dynamic_sensor(r["label"], sens))
