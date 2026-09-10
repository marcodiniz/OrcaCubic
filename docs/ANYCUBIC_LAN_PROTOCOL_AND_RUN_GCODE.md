# Anycubic Kobra X LAN protocol findings and “Run G-code file” design

Status: investigation/design reference for OrcaCubic  
Target printer used for live read-only validation: Anycubic Kobra X, model ID `20030`, firmware `2.0.1.9`  
Date: 2026-09-03

## 1. Scope and evidence

This document records what was established from:

- read-only probes against a Kobra X in LAN mode;
- a passive MQTT subscription and read-only MQTT queries;
- KX-Bridge nightly source at commit `6ea111c5e87865f59729b6671f6a106750e9227f`;
- AnycubicSlicerNext and strings exposed by its `mach_mqtt.dll`;
- current OrcaCubic integration code in `src/slic3r/Utils/AnycubicLink.*`.

No movement, heating, filament, camera, print-control, or configuration command was issued during the protocol audit. Secrets and token-bearing URLs must not be logged.

## 2. Verified printer network surface

A complete point-in-time TCP scan found only:

| Port | Service | Use |
|---:|---|---|
| `9883` | MQTT 3.1.1 over TLS | Reports, read queries, typed printer commands |
| `18088` | Tokenized HTTP video stream | Camera video |
| `18910` | HTTP | `/info`, signed `/ctrl`, and `/gcode_upload` |

The stock printer does not expose SSH, Klipper, Moonraker, or a general-purpose raw G-code console.

### Port 18910

Confirmed flow:

1. `GET http://PRINTER:18910/info`
2. Signed `POST /ctrl?ts=...&nonce=...&sign=...&did=...`
3. AES-CBC decrypt the response to obtain local MQTT connection material.
4. Query MQTT `info/report` for a short-lived `fileUploadurl`.
5. Upload with `POST /gcode_upload?s=<session-token>`.

The upload is multipart form-data with fields:

- `filename`: destination filename;
- `gcode`: file bytes.

Known required/compatible headers include `X-File-Length` and the `X-BBL-*` client metadata used by AnycubicSlicerNext.

The session token in `fileUploadurl` and the token in the camera URL are credentials and must be redacted from logs, diagnostics, and UI error reports.

## 3. MQTT model

Subscribe at QoS 0:

```text
anycubic/anycubicCloud/v1/printer/public/{model_id}/{device_id}/#
```

Outbound requests and controls use either:

```text
anycubic/anycubicCloud/v1/slicer/printer/{model_id}/{device_id}/{type}
anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/{type}
```

General JSON envelope:

```json
{
  "type": "info",
  "action": "query",
  "msgid": "<uuid>",
  "timestamp": 1788474900000,
  "data": null
}
```

Typed reports arrive at:

```text
.../printer/public/{model_id}/{device_id}/{type}/report
```

The broker also emits a generic `.../{device_id}/response` acknowledgement, commonly with `code: 0` and no useful data. It must not be mistaken for the actual typed report.

The firmware wire spelling is `tempature`, not `temperature`.

## 4. Confirmed data available from the printer

The following read requests produced typed reports on the Kobra X:

| Type | Action | Data |
|---|---|---|
| `info` | `query` | Identity, firmware, capabilities, temperatures, job, upload and camera URLs |
| `info` | `statistic-query` | Print count/time and lifetime material use |
| `status` | `query` | Coarse busy/free state |
| `tempature` | `query` | Current and target nozzle/bed temperatures |
| `fan` | `query` | Part-cooling fan percentage |
| `light` | `query` | Light type/status/brightness |
| `peripherie` | `query` | Camera, multicolor system, and USB presence |
| `properties` | `read` | Hardware, bed, channel count, and numeric option settings |
| `extrudeControl` | `getInfo` | Active filament channel and feed state |
| `multiColorBox` | `getInfo` | Slot material/color/status, loaded slot, drying state |
| `print` | `getSliceParam` | Current job slicing data and AMS mapping |
| `skip` | `query_obj` | Already-skipped objects |
| `file` | `listLocal` | Internal printer files |
| `file` | `listUdisk` | Attached USB files |
| `file` | `listVideo` | Timelapse/video files |
| `file` | `fileDetails` | Thumbnail, objects, dimensions, temperatures, speeds, materials, usage |

### Filament identity limitation

The printer reports physical slot properties—material family, RGB color, brand/SKU when available, remaining percentage, and loaded state—but not the exact Orca user-preset identity.

Therefore, OrcaCubic should own the persistent mapping:

```text
physical printer slot -> exact Orca filament preset
```

The native slicer can match material/color/vendor against its already-loaded presets, ask the user once when ambiguous, retain that mapping, use it for `ams_box_mapping`, and pass it to the embedded Workbench UI. This removes KX-Bridge’s manual export/import requirement.

## 5. Raw G-code conclusion

### Interactive raw G-code

No general MQTT action or HTTP endpoint for executing an arbitrary line of G-code was found. KX-Bridge’s Moonraker-compatible `/printer/gcode/script` is only a translator for a small command set. Unknown lines are acknowledged as `ok` but ignored; they are not forwarded to the printer.

Evidence: KX-Bridge `kobrax_moonraker_bridge.py:5631-5753`.

### G-code inside a print file

The printer accepts uploaded `.gcode` files and executes their contents through its normal print-file interpreter. This is the viable mechanism for OrcaCubic’s requested feature:

> A multiline text field that generates a temporary `.gcode` file, uploads it, starts it as a print job, and removes the printer-side file afterward when possible.

This should be called **Run G-code file**, not “Console,” because it is asynchronous file execution and not an interactive command channel.

## 6. Proposed “Run G-code file” UX

Add the feature to the Device tab or printer-control panel:

```text
Run G-code file
┌───────────────────────────────────────────────────────────┐
│ ; One command per line                                    │
│ M104 S180                                                 │
│ G4 P1000                                                  │
│ M104 S0                                                   │
└───────────────────────────────────────────────────────────┘

[ Validate ]  [ Run on printer ]

☑ Delete temporary file from printer after completion
```

Before execution, show:

- parsed command count;
- generated temporary filename;
- warnings grouped by heating, motion, extrusion, and persistence;
- a preview of the exact generated file;
- the selected printer name/IP without exposing credentials.

Suggested temporary filename:

```text
_orcacubic_command_<UTC timestamp>_<random suffix>.gcode
```

The `_orcacubic_command_` prefix permits safe cleanup without touching normal user jobs.

## 7. Validation and allowlist policy

Do not silently promise truly arbitrary commands in the initial version. Use a policy-driven parser and reject unknown commands by default.

### Tier A: low-risk commands

Candidates for the default allowlist, after confirming them on representative files/firmware:

- blank lines and comments;
- `G4` dwell;
- `M400` wait for movement queue;
- `M104` set nozzle temperature without waiting;
- `M109` set/wait nozzle temperature;
- `M140` set bed temperature without waiting;
- `M190` set/wait bed temperature;
- `M106` / `M107` fan control;
- `M220` speed factor;
- `M221` extrusion factor.

Every numeric parameter must be parsed and bounded. At minimum, clamp/reject temperatures against the active machine profile rather than accepting the text verbatim.

### Tier B: motion or extrusion

Commands such as the following require an additional explicit warning/confirmation:

- `G0` / `G1`;
- `G28`;
- `G90` / `G91`;
- `M82` / `M83`;
- `M84`.

For `G0/G1`, validation should inspect axes, extrusion and feed rate. OrcaCubic does not receive trustworthy live X/Y/Z coordinates from the LAN protocol, so it cannot prove that a requested move is safe from the current position. Relative motion is especially risky if prior modal state is unknown.

### Deny by default

Reject:

- unknown commands;
- firmware update/reset or EEPROM/persistent-configuration commands;
- factory reset;
- host/shell/file-inclusion commands;
- commands outside configured axis, temperature, extrusion, or feed-rate limits;
- malformed lines, unsupported expressions, or macros;
- tool changes whose required filament mapping cannot be resolved;
- commands that depend on a raw Klipper console.

Do not implement an unrestricted “expert mode” until execution behavior has been validated on hardware and the user has a reliable recovery path.

## 8. Generated-file rules

The generated file should:

1. Use only validated user lines—never preserve rejected trailing text.
2. Add a clear generated-file header with OrcaCubic version, timestamp, and safety-policy version.
3. Avoid injecting the normal slicer machine start/end G-code automatically; that may home, purge, heat, or move unexpectedly.
4. Preserve line ordering exactly after normalization.
5. End with `M400` when supported so queued movement finishes before the job completes.
6. Avoid adding heater shutdown implicitly unless the UI explicitly offers that option and previews the inserted commands. Silent cleanup commands could conflict with the user’s intent.
7. Compute and send the real file size and MD5 used by the normal Anycubic print-start path.

Example:

```gcode
; generated by OrcaCubic Run G-code file
; policy: run-gcode-v1
; temporary: true
M104 S180
G4 P1000
M104 S0
M400
```

## 9. Execution lifecycle

Recommended state machine:

```text
Draft
  -> Validated
  -> Uploading
  -> Uploaded
  -> Start requested
  -> Start confirmed
  -> Running
  -> Finished | Canceled | Failed | Start rejected
  -> Cleanup pending
  -> Deleted | Cleanup failed
```

### Upload and start

Reuse the normal Anycubic upload/start path rather than creating a second protocol implementation:

1. obtain fresh upload URL/token;
2. upload generated `.gcode`;
3. start it with the complete canonical `print/start` payload;
4. subscribe to `print/report` and `info/report`;
5. correlate the reported filename/task with the generated filename.

Do not use the reduced KX-Bridge WebSocket start payload `{filename, use_ams:false}`. Use the complete print-start shape already expected by the native `AnycubicLink` implementation, including file URL, size, MD5, task settings, and explicit `use_ams: false` for a command file unless tool changes were deliberately supported.

### Completion detection

Treat a matching job’s terminal state as completion:

- success: `finished`;
- canceled/stopped: `canceled` or firmware spelling `stoped`;
- failure: `failed` or a rejected start response.

Do not delete merely because upload or start returned successfully. Wait until the matching printer-side job reaches a terminal state, otherwise the firmware may still be reading the file.

## 10. Printer-side deletion

Deletion is possible through MQTT:

```text
type:   file
action: deleteBatch
data:
  root: local
  files:
    - path: /
      filename: <temporary filename>
```

A generic MQTT acknowledgement is not sufficient. The real result arrives later as `file/report` with action `deleteBatch`; verify its success state/code.

Evidence:

- KX-Bridge `kobrax_moonraker_bridge.py:1630-1668`;
- KX-Bridge `kobrax_moonraker_bridge.py:2972-3057`;
- Anycubic `mach_mqtt.dll` exposes `deleteLocal`, `deleteUdisk`, `deleteVideo`, and `deleteBatch` vocabulary.

### Cleanup policy

Default behavior:

- **Finished successfully:** delete automatically when the checkbox is enabled.
- **Canceled/failed after start:** attempt best-effort deletion only after a terminal report confirms execution stopped.
- **Start rejected:** delete the uploaded temporary file after confirming no matching job is active.
- **Connection lost or outcome unknown:** do not guess. Add the file to a pending-cleanup list and retry after reconnect.
- **Deletion failure:** show a non-blocking warning and retain a retry action.

Cleanup must only target filenames generated by OrcaCubic and recorded in its pending-command registry. Never delete by prefix alone without matching a locally recorded operation ID.

On startup/reconnect, list internal files and reconcile pending operations. Offer cleanup for stale recorded `_orcacubic_command_...gcode` files, but do not remove unrelated files.

## 11. Safety dialog

Before the first run, and again for Tier B commands, show an explicit warning:

> This runs the text as a temporary print file on the selected printer. Commands may heat the nozzle or bed, move axes, extrude filament, collide with an object, or damage the printer. OrcaCubic cannot read a reliable live XYZ position from this printer. Review the generated file and remain near the printer while it runs.

The Run button should remain disabled until validation passes. Motion/extrusion commands should require a second confirmation that includes the exact risky lines.

Provide an immediately accessible **Stop** button using the typed MQTT `print/stop` action. This is not a guarantee of instant physical motion cessation, so the UI must not label it as a hardware emergency stop.

## 12. Implementation seams in OrcaCubic

Recommended separation:

- `AnycubicLink`: persistent MQTT connection, reports, upload/start/delete primitives.
- `AnycubicRunGcodeValidator`: parsing, allowlist, numeric limits, warnings, generated file.
- `AnycubicCommandJob`: lifecycle/correlation and pending cleanup.
- Device-tab UI/WebView handler: text field, preview, confirmations, progress, Stop, cleanup retry.

The current `AnycubicLink` opens short-lived MQTT sessions for token retrieval and print start. This feature needs a persistent, reconnecting session so completion and deletion reports are not missed. Events and polling should feed one idempotent reconciliation path.

Suggested API shape:

```cpp
struct RunGcodeValidation {
    bool valid;
    std::vector<RunGcodeDiagnostic> diagnostics;
    std::string generated_gcode;
    bool contains_motion;
    bool contains_extrusion;
    bool contains_heating;
};

struct AnycubicCommandJob {
    std::string operation_id;
    std::string remote_filename;
    CommandJobState state;
    bool delete_after_completion;
};
```

Required backend operations:

```cpp
validate_run_gcode(text, machine_limits)
upload_temporary_gcode(job, generated_file)
start_temporary_gcode(job)
stop_temporary_gcode(job)
delete_local_files({job.remote_filename})
reconcile_command_jobs(printer_reports, printer_file_list)
```

## 13. Acceptance criteria

The feature is complete only when all are demonstrated:

1. Unknown or denied commands cannot be uploaded through this UI.
2. The exact generated file is previewed before execution.
3. Temperature and numeric limits are enforced from the active machine configuration.
4. Upload and start use fresh credentials/tokens and the complete print payload.
5. The UI correlates execution with the generated filename, not merely any active print.
6. Stop uses the current task ID where available.
7. Successful completion triggers verified `deleteBatch` cleanup when selected.
8. Connection loss leaves a recoverable pending-cleanup record.
9. Restart/reconnect reconciliation never deletes an unrelated user file.
10. Tokens, MQTT credentials, certificates, private keys, and tokenized camera/upload URLs are absent from logs.
11. Hardware tests cover a harmless command file, a rejected command, cancellation, disconnect-before-cleanup, and deletion retry.

## 14. Still-unverified points

Before broadening the allowlist, validate on the physical Kobra X:

- which Marlin-style commands the print-file interpreter actually accepts;
- whether files containing no extrusion and no normal slicer header start and finish consistently;
- whether `M400` is accepted;
- whether heater-only command files finish normally;
- exact terminal report order before deletion is safe;
- behavior if `deleteBatch` runs immediately after `finished`;
- whether the printer requires a minimum file/header structure;
- whether commands are filtered differently between `.gcode` and `.gcode.3mf`.

Until those drills pass, this document is an implementation design grounded in the verified upload/start/delete transport—not proof that every proposed allowlisted G-code is accepted by firmware.
