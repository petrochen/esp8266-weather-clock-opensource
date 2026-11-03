# GitHub firmware updates

Starting with 1.11.0-beta.2, the Update page can find and install a published
firmware image without making the user download a file manually. A six-digit
device PIN still confirms every installation. The first upgrade from older
firmware uses its existing local-file upload page.

## What runs where

The browser reads the release catalog over HTTPS, compares semantic versions,
checks the PIN with the clock, downloads the selected image and validates its
size, SHA-256 and ESP8266 DIO/1 MB image header. It then sends the image through the
existing authenticated `POST /update`. All downloads and hashing run on the
phone/computer; the ESP gets no HTTPS client, GitHub token, catalog parser or
whole-image RAM buffer. Hashing works on a plain HTTP LAN page without Web Crypto.

The catalog has a fixed hardware target: ESP-01S, 1 MB / 64 KB filesystem, DIO,
80 MHz CPU. The release publisher also verifies that target in `BUILD_INFO.txt`.
This is HTTPS delivery plus corruption detection, not firmware signature
enforcement on the device. The local web interface still assumes a trusted LAN.

## Choosing a version (beta.4 and newer)

Use **All**, **Stable** or **Beta**, then choose a specific **Version**. The list
marks the installed and recommended builds. The button names the exact target;
updates, reinstalls and downgrades all use the same six-digit device PIN. A
reinstall/downgrade additionally requires the inline confirmation checkbox.
Changing the selection clears that confirmation. There is no unattended update,
nightly/Dev channel, stored PIN or background check with the page closed.

The selected release shows its date, size, release notes and transition impact.
Unknown settings formats and withdrawn builds are visible but cannot be installed
from the catalog. Only strict `major.minor.patch` and
`major.minor.patch-beta.number` versions are supported. Refresh checks the catalog
again with a unique URL and displays its publication timestamp; GitHub availability
and CDN behavior can still delay discovery. Local-file upload remains available.

### Downgrade compatibility

| Target from beta.4 | GitHub updater afterward | Settings / features |
| --- | --- | --- |
| beta.2–4 | Available | Same stored formats; beta.4 version chooser is absent in beta.2/3 |
| beta.1 | Unavailable | Beta screens retained; return by manually uploading a BIN |
| 1.10.0 | Unavailable | Core settings, PIN and night settings compatible; no UV or extra beta screens |
| Older / unknown | Not audited | Not offered as a verified transition |

The core, PIN and night records were compared in the released source tags; the
beta feature record occupies a separate EEPROM region. Normal saves in 1.10.0
preserve that region, but resetting all settings can erase it. No automatic reset
is required for these audited transitions. A settings JSON export omits secrets
and is not a complete EEPROM backup. Hardware downgrade/return testing is still
required; source compatibility does not prove a successful physical update.

After the image is accepted, the browser waits for a restart (using freshly read
uptime) and the expected installed version. Upload progress reaching 100% is not
completion. A timeout or version mismatch reports an unconfirmed result and
prevents another submission on that page. Check the clock before reloading and
retrying; no automatic reflash is attempted. For a local file, only the restart
and reported version can be checked, since its expected version is unknown.

The firmware retains the ESP8266 core `Update` writer and its image validation.
The HTTP adapter authenticates each chunk, rejects multiple file parts, withholds
the final byte and commits only after the complete authenticated POST. Interrupted
firmware streams do not arm installation. This is not dual-bank automatic rollback;
physical OTA/recovery still needs hardware validation. The advanced filesystem
writer retains the core's filesystem update behavior.

## Publishing a channel

GitHub Releases binaries currently redirect to a download host without browser
CORS headers. The publication workflow mirrors verified binaries into the
**same repository**, on `codex/firmware-updates`. Public `raw.githubusercontent.com`
URLs support browser access. No proxy, GitHub Pages site or additional service is
needed. The branch holds `channels.json`, `releases.json` and `firmware/<sha256>.bin` files; ordinary
source branches do not contain those binaries.

1. Install `.github/workflows/publish-updates.yml`, `tools/prepare_update_channel.py`,
   `tools/update_profiles.json` and `tests/test_update_channel.py` on the default branch before enabling
   publication. Include an audited profile for each new release in
   `tools/update_profiles.json`; do not infer capabilities from version ordering.
   Beta firmware can stay on its release branch. The workflow always
   runs the publisher from the trusted default branch, not from a release-provided script.
2. Publish a GitHub release/prerelease with its `.bin`, `SHA256SUMS` and
   `BUILD_INFO.txt`, produced by the pinned build pipeline. These are the only
   release attachments needed; keep documentation and previews in the repository.
   Upload all three files before publishing the draft. Keep the tag and `Firmware:`
   version consistent, and list only current attachments in `SHA256SUMS` (not itself).
3. The **Publish browser update channel** workflow verifies the release and
   publishes the appropriate catalog entry. Stable and beta remain separate;
   publishing an older version cannot move either channel backwards.
4. On first publication with the new publisher, the workflow also stages the
   four audited historical releases (1.10.0 and beta.1–3). This is part of the same
   commit, so the new UI never sees a partially seeded history.
5. Check that workflow's result after publication. For existing releases or an
   asset-upload race, run it manually with the published tag. Seed both current
   stable and beta tags once when setting up the channel. To add an older audited
   release without changing recommendations, select `history_only` when dispatching
   the workflow (or pass `--history-only` to the staging tool).

The canonical binary is selected using the exact revision in `BUILD_INFO.txt`,
then matched against `SHA256SUMS` and GitHub's asset digest. This also handles a
release retaining a superseded binary. No channel commit occurs on failed checks.
The workflow serializes publications and pushes without force. Repository rules
must permit its token to update only this publication branch as appropriate.

To inspect a release locally without publishing:

```sh
python3 tools/prepare_update_channel.py --tag v1.11.0-beta.1 --output /tmp/clock-update-channel
```

### Catalog compatibility and release profiles

`channels.json` retains schema 1 and its 4,096-byte limit for beta.2/3. The new
`releases.json` uses schema 2, a 32,768-byte limit, the same hardware target,
publication timestamp, stable/beta recommendations and a list of releases.
Each entry has a version, date, canonical build revision, size, SHA-256, status
and audited profile (`storage`, `github`, `forecast`, `version_picker`). Images remain addressed by
hash on the fixed repository host; catalog data cannot redirect firmware downloads.

The current client knows storage profile `eeprom-v1` and permits only its audited
preserving transitions. A future layout/migration needs an explicit client and
publisher change; unknown formats must not silently become compatible. The profile
flags describe the target *after* installation. The currently running firmware
still authenticates and writes the upload using its own protocol.

Re-publication is idempotent and cannot silently replace a recorded image/profile.
A `withdrawn` entry may include a human-readable `reason`; it must also be removed
from the new recommendations and legacy channel pointers before publication.
Choose any replacement recommendation explicitly, auditing the transition for old
clients; the publisher does not bypass its channel downgrade guard. History-only
imports never revive withdrawn entries. Both catalogs and verified images must be
published in one Git commit, with no force push.

Keep existing channel files when staging a subsequent release. The image name is
its content hash, so cached catalogs still refer to their original bytes. GitHub
CDN caching can delay discovery briefly. A missing catalog, failed download or
checksum mismatch leaves the clock unchanged and offers the local-file path.

## Checks

`tests/test_releases.cjs` compares browser SHA-256 with Node's implementation at
padding boundaries and the maximum firmware size, and checks stable/beta ordering, catalog validation and transition consequences.
`tests/test_update_channel.py` checks channel separation, rollback refusal,
canonical revision selection, idempotent history imports, immutable/withdrawn builds
and rejection of mismatched images/metadata.
`tests/test_web.py` runs the actual embedded page over an insecure HTTP origin,
including PIN failures, corrupt/oversized downloads, channel errors, local-file
fallback, version filters, downgrade/reinstall confirmation, unknown/withdrawn
releases, reboot/version verification and desktop/mobile layouts. The host update harness tests byte identity,
deferred commit, interrupted maximum-size images, multiple files, authorization
and writer failures. These simulations do not replace physical OTA testing.
