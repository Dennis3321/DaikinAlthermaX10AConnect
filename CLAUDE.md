# Daikin Altherma X10A Connect - Project Guide

## Project Overview

This is an ESPHome-based integration for Daikin Altherma heat pumps, specifically designed for M5Stack POE (Power over Ethernet) hardware. The project enables Home Assistant to monitor and control Daikin heat pumps via the X10A serial protocol.

### Key Features
- Read multiple heat pump parameters (temperatures, operation modes, etc.)
- Control operation modes: Off/Heat/Cool
- Smart grid feature support (4 modes: Free running, Forced off, Recommended on, Forced on)
- Runtime debug mode toggle via Home Assistant switch
- Built on ESPHome for easy configuration and updates
- Uses PoE for reliable power and network connectivity

## Hardware Setup

**Required Components:**
- M5 Stack ESP32 Ethernet Unit with PoE (SKU: U138)
- M5 Stack 4-Relay Unit (SKU: U097)
- M5 Stack ESP32 Downloader Kit (SKU: A105)
- Compatible Daikin Altherma 3 R F heat pump

**Pin Connections:**
- UART: RX=GPIO3, TX=GPIO1, 9600 baud, EVEN parity
- I2C (for relay unit): SDA=GPIO16, SCL=GPIO17
- Ethernet: IP101 PHY on GPIO23/GPIO18/GPIO0/GPIO5

## Project Structure

```
ESPHomePoeAlterma/
├── components/
│   └── daikin_x10a/           # Custom ESPHome component
│       ├── daikin_x10a.cpp    # Main component: UART communication, register
│       │                      #   management, AND conversion logic
│       ├── daikin_x10a.h      # Component header (DaikinX10A class)
│       ├── daikin_package.h   # Packet handling only (buffer, CRC, protocol parsing)
│       ├── register_definitions.cpp  # Register struct definition
│       ├── register_definitions.h    # Register struct + scan interval constant
│       ├── __init__.py        # ESPHome Python codegen (sensor auto-creation)
│       └── manifest.json
├── m5poe.yaml                 # ESPHome configuration (hardware + registers)
└── README.md                  # Detailed documentation
```

### Configuration

**`m5poe.yaml`** is the single configuration file containing:
- Hardware setup (UART, I2C, Ethernet, relays)
- daikin_x10a component initialization with full register list
- Mode and smart grid controls (template selects)
- Debug mode switch

**Register configuration example (in m5poe.yaml):**
```yaml
daikin_x10a:
  id: daikin_comp
  uart_id: daikin_uart
  mode: 1
  registers:
    # mode: 0 = read only, not visible in HA
    - { mode: 0, registryID: 0x61, offset: 2, ConversionID: 105, dataSize: 2, dataType: 1, label: "Refrig. Temp. liquid side (R3T)" }

    # mode: 1 = read AND auto-create sensor in HA
    - { mode: 1, registryID: 0x61, offset: 10, ConversionID: 105, dataSize: 2, dataType: 1, label: "DHW tank temp. (R5T)" }
    # ... sensors with mode=1 automatically appear in Home Assistant!
```

**No manual sensor definitions needed!** Sensors are auto-created for all `mode: 1` registers.

**Visual flow:**
```
Heat Pump X10A Protocol
         ↓
[daikin_x10a component reads via UART]
         ↓
registers: array in m5poe.yaml
  - { mode: 0, label: "..." }  ← Read but not exposed to HA
  - { mode: 1, label: "DHW tank temp. (R5T)" }  ← AUTO-CREATES SENSOR
         ↓
[Home Assistant Entity] (automatically created for mode=1)
```

## Development Guidelines

### ESPHome Configuration
- Config: `m5poe.yaml` (hardware setup, register definitions, controls)
- Uses external_components to pull from this GitHub repository
- Requires secrets file for API encryption key and OTA password

### Custom Component (daikin_x10a)
- Written in C++ following ESPHome component architecture
- `DaikinX10A` class owns all state: registers (`registers_` member), sensors, and conversion logic
- `daikin_package` is a pure packet class: buffer management, CRC, protocol parsing — no register/conversion knowledge
- Conversion functions (`convert_registry_values_`, `convert_one_`, `convertTable*_`) are methods of `DaikinX10A`
- Exposes register values via `get_register_value()` method
- Supports custom register definitions via YAML configuration
- Register definition format: `{ mode, registryID, offset, ConversionID, dataSize, dataType, label }` (+ optional: `unit`, `device_class`, `accuracy_decimals`)

**Dynamic Sensor Registration (IMPLEMENTED):**
- `mode: 0` = Read register but don't expose to HA (data available via `get_register_value()`)
- `mode: 1` = Read register AND automatically create sensor in HA
- No manual template sensor definitions needed for mode=1 registers
- Sensors automatically appear in Home Assistant based on register label

### Code Conventions
- Use ESPHome YAML syntax and conventions
- C++ code follows ESPHome component patterns
- Lambda functions in YAML for sensor value extraction
- Internal switches for relay control (not exposed to HA directly)
- Template selects for user-facing controls

### Relay Logic
**Relay assignments:**
- R1, R2: Heat pump mode control (Off/Cooling/Heating)
- R3, R4: Smart grid control (4-state logic)

**Safety pattern:** Always turn off relays before changing states:
```cpp
id(r1).turn_off();
id(r2).turn_off();
// Then set new state
```

## Important Notes

### UART Configuration
- Baud rate: 9600
- Parity: EVEN
- Stop bits: 1
- Debug mode available but commented out by default

### Ethernet (PoE)
- Type: IP101
- Power delivered via PoE eliminates need for separate power supply
- PIN 1 of X10A connector NOT used (PoE provides power)

### State Restoration
- Relays use `RESTORE_DEFAULT_OFF` for safety
- Ensures heat pump doesn't activate unexpectedly on power cycle

### Compatibility
Project is designed for specific Daikin models. See README.md for full list of compatible models (ERGA, EHVH, EHVX series).

## Building and Flashing

### Build Process (step by step)

The code is edited locally on Windows and built remotely on Home Assistant. There is NO local compilation.

```
Windows (VS Code)                    Home Assistant (ESPHome)
─────────────────                    ───────────────────────
1. Edit code in VS Code
2. Commit (GitHub Desktop)
3. Push to GitHub              →     GitHub repo updated
                                     4. Click "Clean Build Files"
                                        (removes cached components)
                                     5. Click "Install"
                                        → ESPHome fetches component from GitHub
                                        → Compiles firmware
                                        → Flashes to ESP32 via OTA
```

**Step-by-step:**
1. **Edit** - Wijzig code in VS Code op Windows (C++, YAML, Python bestanden)
2. **Commit** - Commit via GitHub Desktop
3. **Push** - Push naar GitHub repository (Dennis3321/ESPHomePoeAlterma)
4. **Clean** - In Home Assistant ESPHome dashboard: klik "Clean Build Files" om gecachte external components te verwijderen. Dit is nodig omdat ESPHome anders mogelijk een oude versie van het component gebruikt
5. **Install** - Klik "Install" in ESPHome dashboard. ESPHome haalt het component vers op van GitHub (`external_components` met `refresh: 0s`), compileert alles, en flasht via OTA naar het device

**Belangrijk:** Claude kan alleen stap 1 (editen) doen. Na code-wijzigingen moet de gebruiker zelf committen, pushen, en in Home Assistant builden.

### First-Time Setup
1. Eerste flash vereist M5Stack ESP32 Downloader Kit (SKU: A105) via USB
2. Alle updates daarna gaan via OTA (over-the-air) vanuit Home Assistant

## Testing

When modifying the custom component:
- Test with actual hardware or use UART debug mode
- Verify register value parsing returns valid data
- Check sensor values appear correctly in Home Assistant
- Test relay state changes don't cause unexpected behavior

## Common Tasks

### Adding New Sensors
To make a new sensor visible in Home Assistant, simply set `mode: 1` in the register definition:

**Example in m5poe.yaml:**
```yaml
daikin_x10a:
  uart_id: daikin_uart
  mode: 1
  registers:
    # This sensor will automatically appear in Home Assistant
    - { mode: 1, registryID: 0x61, offset: 10, ConversionID: 105, dataSize: 2, dataType: 1, label: "DHW tank temp. (R5T)" }

    # This register is read but NOT shown in HA (useful for internal calculations)
    - { mode: 0, registryID: 0x60, offset: 5, ConversionID: 203, dataSize: 1, dataType: -1, label: "Error type" }
```

**That's it!** No manual sensor definitions needed.

**Key points:**
- `mode: 1` = Sensor automatically created in Home Assistant
- `mode: 0` = Data read but not exposed to HA (still accessible via `get_register_value()`)
- Sensor name in HA = the `label` field
- Sensor unit auto-detected from dataType: 1=temperature(°C), 2=pressure(bar), 3=current(A), -1=no unit
- Labels must be unique

### Modifying Component
1. Edit files in `components/daikin_x10a/`
2. Component auto-updates from GitHub on ESPHome compilation
3. Can use local path instead of GitHub URL for testing

### Modifying Register Configuration
1. Edit the `registers:` array in `m5poe.yaml`
2. Register fields:
   - `mode`: **0** (read but don't show in HA) or **1** (read AND auto-create sensor in HA)
   - `registryID`: X10A register address (e.g., 0x61)
   - `offset`: Byte offset within register
   - `ConversionID`: Conversion ID for data interpretation (see convert_one_ in daikin_x10a.cpp)
   - `dataSize`: Number of bytes to read (1 = 8-bit, 2 = 16-bit)
   - `dataType`: Sensor type hint: -1 = generic, 1 = temperature, 2 = pressure, 3 = current
   - `label`: Human-readable name used as sensor name in HA - MUST be unique!
3. Simply change `mode: 0` to `mode: 1` → sensor appears in Home Assistant!

### Debug Mode
The component has a runtime debug mode controlled via a Home Assistant switch ("Daikin Debug Mode"):
- **Off (default):** No UART logging — keeps ESPHome logs clean
- **On:** Enables all `ESP_LOGI` logging in `FetchRegisters()`, `process_frame_()`, and `convert_registry_values_()` (TX/RX packets, registry decoding, conversion details, CRC errors, etc.)
- Startup logs (sensor registration) always appear regardless of debug mode
- Implemented via `debug_mode_` bool in `DaikinX10A` class, toggled by `set_debug_mode(bool)`
- Switch defined in `m5poe.yaml` as a template switch with `restore_mode: RESTORE_DEFAULT_OFF`

### Debugging UART Communication (low-level)
Uncomment debug section in `m5poe.yaml` for raw UART byte logging:
```yaml
#debug:
#  direction: BOTH
#  dummy_receiver: true
#  after:
#    bytes: 16
```

## Known Limitations & Future Improvements

### Sensor Customization (IMPLEMENTED)
**Auto-detection based on dataType:**
- `dataType: -1` = generic (no unit)
- `dataType: 1` + temperature ConversionID = °C, device_class: temperature
- `dataType: 2` = bar, device_class: pressure
- `dataType: 3` = A, device_class: current

**Optional overrides in register definition:**
- `unit`, `device_class`, `accuracy_decimals` fields override auto-detection
- Example: `{ mode: 1, ..., unit: "kW", device_class: "power", accuracy_decimals: 2 }`

## Safety and Disclaimers

- Project is for educational/experimental purposes
- Modifying heat pump connections can void warranty
- Always follow heat pump safety manual
- No support or updates guaranteed
- Use at your own risk

## External Dependencies

- ESPHome framework
- M5Stack ESPHome components (for Unit4Relay)
- Home Assistant for integration
- GitHub repository for component distribution
