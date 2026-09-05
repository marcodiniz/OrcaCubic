# OrcaCubic

**OrcaCubic is an experimental fork of [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) focused on direct, local integration with the Anycubic Kobra X and its four-slot filament system.**

It combines OrcaSlicer's slicing workflow with selected printer-integration ideas from Anycubic Slicer Next and KX-Bridge. The goal is a single desktop application for slicing, material matching, LAN upload, print start, monitoring, and printer control—without requiring the Anycubic cloud or a separately installed bridge service.

> [!IMPORTANT]
> OrcaCubic has been tested on an **Anycubic Kobra X running firmware 2.0.1.9**. Other Anycubic models and firmware versions are not yet validated.

## Download

Download the current installer or portable build from [GitHub Releases](https://github.com/marcodiniz/OrcaCubic/releases/latest).

The release is currently unsigned. Windows may show a SmartScreen warning; verify the SHA-256 checksums published with the release before running it.

## What OrcaCubic adds

### Native Kobra X LAN printing

- Connects directly to the printer's stock LAN services.
- Uploads `.gcode` and `.gcode.3mf` jobs over the local network.
- Starts prints with explicit ACE slot mappings.
- Uses a bundled local bridge process that starts automatically for the configured printer—no separate KX-Bridge, Moonraker, OctoPrint, or Anycubic cloud session is required.
- Keeps printer credentials and session tokens local and redacts token-bearing URLs from logs and dashboard status responses.

### Remote Print material matching

Before a multi-material job starts, OrcaCubic shows the project tools and live printer slots so each sliced color can be assigned explicitly. It auto-suggests compatible slots by material and color, allows manual changes, and blocks invalid material mappings instead of silently choosing another slot.

<!-- SCREENSHOT: Add the Remote Print / filament matching popup here.
Suggested file: docs/images/remote-print-material-matching.png
![Remote Print material matching](docs/images/remote-print-material-matching.png)
-->

### Local Device dashboard

The Device tab is a local Anycubic-style dashboard with:

- live print progress, layer, elapsed-time, and remaining-time telemetry;
- opt-in camera streaming and chamber/camera light controls;
- nozzle and bed temperatures, fan control, cooldown, and preheat presets;
- Quiet, Standard, Sport, and Ludicrous speed modes;
- pause, resume, and cancel controls;
- X/Y/Z movement, homing, motor release, extrusion, and retraction;
- a controlled **Run G-code File** workflow;
- device information and printer alerts;
- movement controls locked while a job is printing or paused.

<!-- SCREENSHOT: Add the full Device dashboard here.
Suggested file: docs/images/device-dashboard.png
![OrcaCubic Device dashboard](docs/images/device-dashboard.png)
-->

### Filament synchronization and editor

- **From Slicer** sends the current OrcaCubic filament definitions and colors to the printer's four slots.
- **To Slicer** imports the printer's live slot materials and colors into the slicer.
- Solid, gradient, and luminous color definitions are supported.
- The material picker includes material, brand, finish, swatches, custom colors, and spool preview.
- Slot writes are paced for Kobra X firmware reliability.

<!-- SCREENSHOT: Add the filament picker here.
Suggested file: docs/images/filament-picker.png
![Filament picker](docs/images/filament-picker.png)
-->

### Kobra X-specific safety and reliability work

- Persistent MQTT lifecycle avoids the Windows socket teardown crash seen with short-lived print-start sessions.
- Printer upload and camera credentials are not shown in UI errors or diagnostic logs.
- Camera streaming is strictly opt-in and remains stopped when the Device tab opens or reloads.
- The four-entry ACE mapping preserves slicer tool-to-slot assignments and avoids hidden Slot 3 fallbacks.
- The Remote Print dialog exposes calibration switches, although firmware startup macro `G9111` may still perform firmware-defined preparation independently of those task flags.

## Setup

1. Put the Kobra X and the computer on the same trusted local network.
2. In OrcaCubic, select or create the **Anycubic Kobra X** printer profile.
3. Open **Printer settings → Physical printer**.
4. Choose **Anycubic** as the print host type and enter the printer's LAN IP address.
5. Use **Test** to verify the direct connection.
6. Slice a plate, select **Print**, review the material-to-ACE mappings, and start the job.

The bundled bridge listens only on `127.0.0.1` and obtains the printer-specific connection data through the Kobra X LAN handshake at runtime. No printer IP, device ID, CN, certificate, private key, password, or session token is embedded in the public source or release.

## Known limitations

- Hardware validation currently covers only Kobra X firmware `2.0.1.9`.
- The bundled LAN bridge is part of OrcaCubic's current architecture and may be replaced by an entirely native implementation later.
- The printer's `G9111` startup macro can run firmware-defined preparation even when a corresponding Remote Print calibration flag is disabled.
- Releases are not code-signed yet.

## Screenshots wanted

The README deliberately includes placeholders for:

- the Remote Print filament-matching popup;
- the complete Device dashboard;
- the filament picker/editor.

Add images under `docs/images/` and uncomment the matching Markdown blocks above.

## Build from source

See [BUILD_WIN.md](BUILD_WIN.md) for the Windows build used by this fork. Upstream build information remains available in the [OrcaSlicer documentation](https://github.com/OrcaSlicer/OrcaSlicer/wiki/How-to-build).

## Thanks and upstream projects

OrcaCubic exists because of these projects and their contributors:

- [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) — the upstream slicer and foundation of this fork.
- [AnycubicSlicerNext](https://github.com/ANYCUBIC-3D/AnycubicSlicerNext) — reference for Anycubic-oriented workflows, UI behavior, and printer integration.
- [KX-Bridge](https://gitea.it-drui.de/viewit/KX-Bridge-Release) — Kobra X LAN protocol research and bridge behavior that informed this integration.

Thank you to all maintainers, reverse engineers, testers, and contributors who made that work available.

## License

OrcaCubic follows OrcaSlicer's GNU Affero General Public License v3.0 terms. See [LICENSE.txt](LICENSE.txt). Bundled third-party components retain their own licenses; the Python bridge's vendored dependency notices are in `resources/scripts/vendor/LICENSES/`.

OrcaCubic is an independent community project. It is not affiliated with or endorsed by Anycubic or the OrcaSlicer maintainers.
