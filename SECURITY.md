# Security policy

## Reporting a vulnerability

Please **don't open a public issue**. Report it privately instead:
**Security → Report a vulnerability** on this repository
([direct link](https://github.com/rightiswrong/m5nanoc6-zigbee-lighting-control/security/advisories/new)).
You'll get a reply within a week. Fixes ship as a normal release, with an advisory once users can update.

In scope: the firmware, the Home Assistant integration (`custom_components/spooky_lights`),
the Zigbee2MQTT converter, the tools, and the build/release workflows.

## Supported versions

Only the latest release gets fixes. Update with the `-app.bin` (keeps pairing) or the
`-factory.bin`.

## Verifying downloads

Release files carry signed build-provenance attestations (from 1.1.4 on):

```bash
gh attestation verify spooky-nanoc6-matter-v1.1.4-factory.bin --repo rightiswrong/m5nanoc6-zigbee-lighting-control
```

`SHA256SUMS.txt` only detects a corrupted download. The attestation is what proves the
file came from this repository's release workflow.

## Known limitations (by design, for a hobby device)

* **Physical access = control.** The stock images don't enable flash encryption or
  secure boot, so anyone with a USB cable can read the flash. That includes your
  Thread/Zigbee network key and Wi-Fi password. Lock your own boards down with
  [docs/HARDENING.md](docs/HARDENING.md).
* **Matter test identity.** Matter builds use the test vendor ID 0xFFF1 and the example
  attestation certificate, so commissioners show the light as uncertified.
* **Zigbee joins with the standard default link key** (install codes aren't supported by
  the Arduino Zigbee library yet).
* **Pairing codes** are printed on the serial console. Blank them out of any log you share.
