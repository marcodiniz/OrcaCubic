# Kobra S1 with Rinkhals — experimental beta

This profile is for a Kobra S1 running Rinkhals, reached directly through its Moonraker endpoint. It is not the stock S1 profile and is not intended for KX-Bridge. Existing stock S1 and Kobra X profiles remain unchanged.

## Before printing

- This is a **single-material beta**. ACE/multicolor switching is not validated or supported by this profile.
- Select `Anycubic Kobra S1 (Rinkhals)` with its 0.4 mm nozzle, 0.20 mm Standard process and PLA preset. Use the matching presets, not settings retained from an old project.
- Configure the Moonraker address reachable from your computer (including the correct port). For a cloud PC, retain your private Tailscale route. Do not expose Moonraker publicly.
- The printer must have a saved mesh named `default` and a working `PAUSE` macro. Verify homing, Z-offset and fan controls against your installed firmware before printing.
- Load and prime the filament manually before starting. The profile does not perform stock chute cleaning, cutting or automatic filament loading. Two skirt loops provide a limited extrusion check, not a substitute for priming.
- Output is plain `.gcode`. Stock `G9111`, `M601` and out-of-bed end parking are absent. The end sequence waits for moves, disables heaters and the part fan, then disables motors without moving to an assumed parking location.
- For this model only, the G-code format-identification header uses OrcaSlicer so existing Rinkhals metadata readers recognize it; a separate comment retains the actual OrcaCubic generator identity.

## Test procedure

1. First export and inspect a small single-material file. Confirm temperatures, homing, mesh load and generated start/end commands. Do not print until the owner has checked these for their firmware.
2. Choose **Upload only**. Record slicer version, Rinkhals version, endpoint/port, filename, file size and the time the upload completes.
3. Wait for the file to appear in the printer UI and for metadata processing to finish. If this fails or stalls, save Moonraker and GoKlipper logs; do not repeatedly upload or start jobs.
4. With the printer supervised and ready, start the small file manually once. If the result is ambiguous, check printer state before doing anything else.
5. Only after that succeeds, test Upload & Print once. No automatic start retries or fixed sleep workaround have been added.

Share logs with API keys, passwords, tokens and other credentials removed. For a speed comparison, upload identical bytes to the same endpoint with both slicers and record their versions and timestamps. A direct Tailscale connection rules out a DERP relay for that session, not every possible network bottleneck.

## What this beta does not fix

The supplied logs show repeated cancellations of `mmu_ace_metadata.py` and `ValueError: No enum member named flow_cali` inside Moonraker callbacks. These components run on the printer, not in OrcaCubic. Header compatibility may improve format recognition, but is not a demonstrated fix for their cancellations or for error 10115.

The cited 00:33 upload is followed by `Job Started`, so its short upload-to-start interval does not prove a race. Other attempts do fail with 10115; delayed/manual-start results and file/path visibility are still needed to isolate that failure. Metadata lookup errors for `.3mf_temp/...` are separate evidence and do not by themselves mean the print failed.
