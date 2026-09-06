# Future feature: Anycubic Cloud printing

## Status

**Investigation only — not implemented or supported.**

OrcaCubic currently prints to the Kobra X over the local network. A future optional cloud mode could allow users to send and monitor prints when the computer and printer are not on the same network.

The investigation found that this is technically possible, but it would depend on Anycubic's private cloud service. It should therefore be treated as an experimental option rather than replacing local-network printing.

## User experience under consideration

A future cloud mode could provide:

- sign-in with an Anycubic account;
- selection of a Kobra X already linked to that account;
- upload and print from outside the printer's local network;
- ACE color-to-slot matching before print start;
- print progress and remote pause, resume, and cancel controls;
- a choice between keeping the uploaded file in the user's cloud library or removing it after print start.

Local-network printing would remain the default because it works without an Anycubic account or internet connection.

## Expected workflow

1. The user signs in and selects a linked printer.
2. OrcaCubic requests temporary cloud storage for the sliced file.
3. The file is uploaded to Anycubic's storage.
4. Anycubic processes the upload and returns a cloud file record.
5. OrcaCubic submits the print request with the target printer and ACE slot assignments.
6. OrcaCubic monitors the job through Anycubic's service.

An open-source community client already demonstrates the major cloud-storage and print-start operations. However, a complete third-party cloud print has not yet been verified on a Kobra X for this project.

## Known challenges

### Account sign-in

Anycubic's login includes CAPTCHA and two-factor authentication. Community clients generally import an existing session token from Anycubic Slicer Next, the Anycubic website, or the mobile app instead of accepting an email address and password directly.

A suitable OrcaCubic design must provide a safe user-facing sign-in or token-import flow without exposing credentials in logs or configuration exports.

### Undocumented service

Anycubic does not publish a supported third-party cloud API. Endpoints, request formats, token rules, storage behavior, and printer commands can change without notice.

Cloud printing could therefore stop working after a server-side change even when OrcaCubic itself has not changed.

### File compatibility

Raw `.gcode` upload is represented in the community implementation. OrcaCubic also needs `.gcode.3mf` support, which has not yet been proven through the third-party cloud path.

### Material and calibration settings

The cloud print format can carry ACE material mappings, but the following options still require verification against a sanitized Kobra X request from the official slicer:

- Auto Leveling;
- Resonance Compensation;
- Flow Calibration;
- Time-lapse.

### Privacy and security

Unlike local-network printing, cloud printing sends the sliced model and job information to Anycubic's systems. Before a cloud upload, OrcaCubic should clearly tell the user that the file is leaving the local network.

Account tokens provide remote access to linked printers and must be stored using operating-system credential protection. Tokens, printer identifiers, upload URLs, and other credentials must be excluded from logs, diagnostics, screenshots, and error reports.

Real-time cloud updates may require Anycubic-owned client identity files. OrcaCubic should not redistribute those files without explicit permission. A first implementation should prefer ordinary web requests and status polling if that can support upload and print start reliably.

## Required validation before implementation

- [ ] Confirm that a Kobra X in cloud mode can be listed through a sanitized client.
- [ ] Upload a harmless `.gcode` file without starting it.
- [ ] Determine whether `.gcode.3mf` files upload and parse correctly.
- [ ] Start and cancel a small single-color test print.
- [ ] Start a multi-color test using non-sequential ACE slots, such as Slots 1 and 4.
- [ ] Capture sanitized official requests for every Remote Print calibration option.
- [ ] Determine whether web sign-in is sufficient or an Anycubic Slicer Next token is required.
- [ ] Verify expired-session handling, sign-out, and reauthentication.
- [ ] Verify temporary-file removal and cleanup after failed uploads.
- [ ] Test internet loss, Anycubic service errors, and printer-offline behavior.
- [ ] Confirm acceptable third-party use with Anycubic before publishing the feature.

## Recommendation

If pursued, implement this as a separate **Anycubic Cloud (Experimental)** connection mode with:

- explicit user consent before cloud upload;
- secure credential storage;
- clear separation from local-network printers;
- no dependency on closed Anycubic desktop plugins;
- no redistribution of Anycubic-owned private keys or certificates;
- honest error messages when Anycubic's service is unavailable or has changed.

Do not advertise cloud printing as supported until real Kobra X tests cover single-color printing, multi-color ACE mapping, cancellation, expired credentials, temporary-file cleanup, and service outages.

## References

- [Anycubic Slicer Next quick-start guide](https://wiki.anycubic.com/en/software-and-app/new-page-anycubic-slicer-beta%28orca-version%29/anycubic-slicer-next-slicing-software-quick-start-guide)
- [Anycubic app listing](https://play.google.com/store/apps/details?id=ac.cloud.com&hl=en)
- [AnycubicSlicerNext source repository](https://github.com/ANYCUBIC-3D/AnycubicSlicerNext)
- [Community Anycubic Cloud API client](https://github.com/Nino6689/anycubic-cloud-api)
- [Community Anycubic Home Assistant integration](https://github.com/Nino6689/hass-anycubic)
- [Anycubic privacy policy](https://store.anycubic.com/pages/privacy-policy)
- [Anycubic Cloud security incident statement](https://eu.anycubic.com/blogs/news/security-issue-of-anycubic-cloud)
