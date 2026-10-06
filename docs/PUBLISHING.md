# Getting Spooky Lights into HACS

HACS distributes Home Assistant *integrations*, not microcontroller firmware. So
the HACS-installable part of this repo is `custom_components/spooky_lights`;
the firmware lives alongside it in the same repo and is linked from the README.
That layout (one integration per repo, under `custom_components/<domain>/`) is
exactly what HACS expects.

## Checklist

1. **Manifest links** already point at
   [`rightiswrong/m5nanoc6-zigbee-lighting-control`](https://github.com/rightiswrong/m5nanoc6-zigbee-lighting-control).
2. **Repository settings** — the repo must be public; then in its settings:
   * add a **description**,
   * enable **Issues**,
   * add **topics**, e.g. `home-assistant`, `hacs`, `hacs-integration`,
     `zigbee`, `esp32-c6`, `halloween`.
3. **Brand icon** — already included in `custom_components/spooky_lights/brand/`
   (`icon.png` 256 px, `icon@2x.png` 512 px). Home Assistant 2026.3+ reads it
   from there, so no PR to `home-assistant/brands` is needed.
   Regenerate with `python3 tools/make_brand.py`.
4. **Let the Actions run** (`.github/workflows/validate.yml`): *HACS validation*
   and *Hassfest* must pass. The other jobs test the firmware and logic.
5. **Publish a GitHub release** (a full release, not just a tag). This is
   automatic: `.github/workflows/release.yml` publishes `v<version>` with all
   binaries the first time `main` carries a `manifest.json` version that has
   no tag yet (pushing a `v*` tag yourself works too).
6. **Test as a custom repository**: HACS → ⋮ → *Custom repositories* → your
   repo URL, type *Integration* → Download → restart HA.
7. **Submit to the default list**: fork `hacs/default`, add
   `"rightiswrong/m5nanoc6-zigbee-lighting-control"` to the `integration` file in
   alphabetical order, and open a PR filling in the template completely.
   Only the repository owner (or a major contributor) may submit, and the PR
   must allow edits by maintainers.

## Releasing a new version

1. Bump `"version"` in `manifest.json`.
2. If you changed effects/endpoints in the firmware, the sync tests in
   `tests/test_logic.py` will fail until `logic.py` matches — that is on purpose.
3. Bump `FW_VERSION` in `firmware/NanoC6_SpookyLights/config.h` to the same
   number (the release refuses to publish if they differ).
4. Push to `main`. The Release workflow builds the firmware, creates the tag
   `vX.Y.Z` and publishes the release with binaries.
