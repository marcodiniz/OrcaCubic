# Anycubic Kobra X Firmware Commands, Architecture & Toolchange Analysis

**Author:** OrcaCubic Project  
**Target Hardware:** Anycubic Kobra X (`K4Pro`, Model ID `20030`, Firmware `2.0.1.9+`) & Anycubic Kobra Series  
**Date:** September 2026  

---

## 1. Firmware Architecture: GoKlipper vs. KlipperC++ (Avata)

Anycubic's firmware development is split across two completely different architectures:

| Property | Kobra 3, 3 Combo, S1, 3 Max | Kobra X (`K4Pro`, Model 20030) |
|---|---|---|
| **Software Base** | **GoKlipper** (Golang reimplementation of Klipper `klippy`) | **KlipperC++ / Avata** (C++ high-performance port) |
| **Core Daemons** | `gklib` (motion & Klipper core), `gkapi` (MQTT/IPC proxy), `k3sysui` (Qt UI) | `avata_main` (C++ motion core), `avata_ui` (UI), `avata_video` (FLV streaming) |
| **G-code Channel** | Intercepts Klipper G-code macros and console commands | Strict G-code subset; non-standard commands handled via MQTT IPC |
| **Security & Signing**| Standard OTA SWU packages | Mandatory **RSA Signature Verification** (`setup.sig` validated against `/etc/ssl/public_key.pem`) |
| **Filament Switching** | Emulated G-code macros (`FEED_FILAMENT`, `SET_CURRENT_FILAMENT`) | Native MQTT IPC subsystem (`extrudeControl`) |

---

## 2. Deep Dive: Key Firmware Commands & Parameters

### A. `G9111` — Print Preparation & Start Macro
* **Implementation Source:** GoKlipper `project/printer_marco.go:Cmd_G9111` & machine configuration files (`config/printer_*.cfg`).
* **Parameter Syntax:** `G9111` accepts space-separated `key=value` pairs. Parameter names are case-insensitive:

| Parameter | Type | Default | Description |
|---|---|---|---|
| **`bedTemp`** / `BEDTEMP` | Float | `60.0` | Target heated bed temperature in °C. |
| **`extruderTemp`** / `EXTRUDERTEMP` | Float | `220.0` | Final printing nozzle temperature in °C. |
| **`wipeTemp`** / `WIPETEMP` | Float | Config `[leviQ3] extru_temp` *(~190°C)* | Intermediate nozzle temperature used during wiping to avoid excessive oozing. |

* **Standard Call Emitted by Slicers:**
  ```gcode
  G9111 bedTemp=60 extruderTemp=215
  ```

* **Step-by-Step Internal Sequence:**
  1. `M104 S<wipeTemp> I1` — Preheats nozzle to wiping temperature (non-blocking).
  2. `M140 S<bedTemp> I1` — Sets bed target temperature.
  3. `HOME_XY` — Sensorless homing of X and Y axes.
  4. `M109 S<wipeTemp> I1` — Waits for nozzle to reach wipe temperature.
  5. `G9112` — Executes nozzle cleaning routine:
     - `G90` (absolute positioning)
     - `G28 W` (sensorless homing verify)
     - `M106 S255` (part cooling fan at 100% to freeze nozzle ooze)
     - `WIPE_NOZZLE` (back-and-forth passes over the silicone brush)
  6. `MOVE_HEAT_POS` — Parks nozzle over the purge/chute area.
  7. `M109 S<extruderTemp> I1` — Waits for nozzle to reach target print temperature.
  8. `UNDERLINE` — Purges and prints the initial prime line on the build plate.

---

### B. `BED_MESH_PROFILE` — Mesh Storage Management
* **Implementation Source:** GoKlipper `project/extras_bed_mesh.go:Cmd_BED_MESH_PROFILE`.
* **Parameter Syntax:** Accepts one of three mutually exclusive actions:

| Action Parameter | Value | Example | Behavior |
|---|---|---|---|
| **`LOAD`** | `<profile_name>` | `BED_MESH_PROFILE LOAD="default"` | Loads the specified mesh profile into active memory. |
| **`SAVE`** | `<profile_name>` | `BED_MESH_PROFILE SAVE="my_mesh"` | Saves the current probed mesh. **Note:** `SAVE="default"` is rejected because `default` is reserved by firmware. |
| **`REMOVE`** | `<profile_name>` | `BED_MESH_PROFILE REMOVE="my_mesh"` | Deletes the specified mesh profile from storage. |

* **Related Mesh Commands:**
  * `BED_MESH_CLEAR`: Takes no parameters; deactivates the current mesh lookup.
  * `BED_MESH_OFFSET [X=<float>] [Y=<float>]`: Applies dynamic X/Y offsets to the mesh.
  * `BED_MESH_CALIBRATE`: Probes the bed and generates a heightmap. Accepts `PROFILE=<name>`, `MESH_MIN=<x,y>`, `MESH_MAX=<x,y>`, `PROBE_COUNT=<x,y>`, `ALGORITHM=<lagrange|bicubic>`, and `RELATIVE_REFERENCE_INDEX=<int>`.

---

### C. `LEVIQ2_AUTO_ZOFFSET` & `LEVIQ_AUTO_ZOFFSET`
* **Implementation Source:** GoKlipper `project/cs1237.go` & `project/leviQ3.go`.
* **Parameters Accepted:** **None**. Both are zero-argument action commands. Any supplied parameters are silently ignored.
* **Hardware Mechanism:**
  * The Kobra printhead uses an integrated CS1237 ADC strain gauge (load cell) mounted directly behind the hotend heatbreak.
  * When `LEVIQ2_AUTO_ZOFFSET` executes:
    1. The printhead moves to the sensor calibration position.
    2. The nozzle gently taps the print surface until the strain gauge detects physical contact.
    3. The firmware calculates dynamic thermal expansion and bed deformation (`tempZOffset` and `Deformation compensation zoffset`).
    4. It writes the resulting value directly into `[probe] z_offset` inside `/userdata/app/gk/printer_data/config/printer_mutable.cfg`.
* **Recommended Manual Sequence (when replacing G9111):**
  ```gcode
  BED_MESH_PROFILE LOAD="default"
  BED_MESH_CLEAR
  LEVIQ2_AUTO_ZOFFSET     ; Executes strain-gauge tap and recalculates probe Z-offset
  M400
  BED_MESH_PROFILE LOAD="default"
  ```

---

## 3. Analysis: The Kobra X Initial Tool "Double Purge" Problem

### The Observed Problem
When printing a model sliced with **Slot 3** (or any non-default slot), even if the printer physically has Slot 3 loaded and ready:
1. The printer starts the print job by heating up and purging **Slot 1**.
2. It prints the initial prime line with **Slot 1**.
3. Immediately after `G9111` completes, the printer pauses, cuts Slot 1, retracts it, switches to **Slot 3**, feeds Slot 3, purges a second time, and only then starts the model.

### The Root Cause
Two factors combine to cause this behavior:

1. **Physical Selector State on Idle / Boot:**
   The Kobra X toolhead has a motorized 4-channel revolving selector. On boot or after an idle cycle, the toolhead channel defaults to **Channel 0 (Slot 1)**.
2. **Start G-code Execution Timing:**
   In stock machine profiles, `machine_start_gcode` executes:
   ```gcode
   G9111 bedTemp=[first_layer_bed_temperature] extruderTemp=[first_layer_temperature[initial_tool]]
   ```
   At this point, the slicer has **not issued any tool command yet**.
   Inside `G9111`, the firmware runs `WIPE_NOZZLE` and `UNDERLINE` using whichever channel is currently engaged (Channel 0 / Slot 1).
3. **Delayed Tool Command Emission:**
   OrcaSlicer emits the initial tool command (`T2` for Slot 3) **after** `machine_start_gcode` finishes.
   When the printer encounters `T2`, its firmware detects that `active_channel != 2`, halting the print to perform a full toolchange (cut, retract, switch, feed, purge).

---

## 4. The Solution: Pre-engaging the Active Channel via `extrudeControl`

On the Kobra X, channel selection is controlled over LAN/MQTT via the **`extrudeControl`** service:

### Wire Protocol Contract

* **MQTT Control Topic:**
  ```text
  anycubic/anycubicCloud/v1/web/printer/20030/<device_id>/extrudeControl
  ```
* **Channel Switch Action (`switchChannel`):**
  ```json
  {
    "type": "extrudeControl",
    "action": "switchChannel",
    "msgid": "<uuid>",
    "timestamp": 1788820000000,
    "data": {
      "index": 2    // 0 = Slot 1, 1 = Slot 2, 2 = Slot 3, 3 = Slot 4
    }
  }
  ```
* **Status Query Action (`getInfo`):**
  ```json
  {
    "type": "extrudeControl",
    "action": "getInfo",
    "msgid": "<uuid>",
    "timestamp": 1788820000000,
    "data": null
  }
  ```
  * **Report (`.../extrudeControl/report`):**
    ```json
    {
      "type": "extrudeControl",
      "action": "getInfo",
      "code": 200,
      "data": {
        "index": 2,              // Currently engaged channel (0..3, or -1 if unengaged)
        "has_filaments": 1,      // Whether filament is loaded in the extruder
        "current_status": 0,
        "move_type": 0
      }
    }
    ```

### OrcaCubic "Pre-engage Filament" Integration

To eliminate the double purge:
1. The **Remote Print** dialog provides a **"Pre-engage Filament"** checkbox (enabled by default).
2. Before `start_print_job` is dispatched, OrcaCubic determines the physical slot corresponding to the print's initial tool (e.g. physical Slot 3 = index `2`).
3. OrcaCubic publishes `extrudeControl:switchChannel` with `{"index": initial_slot}` over the persistent MQTT LAN bridge.
4. The toolhead selector motor rotates and locks onto the correct channel *before* `G9111` heats and primes.
5. When `G9111` executes, heating, wiping, and prime-line extrusion occur directly with the intended filament, completely avoiding the initial cut, switch, and secondary purge.
