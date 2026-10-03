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

Stable excludes prereleases. Stable + Beta selects whichever supported version is
newer, including a final stable release that supersedes a beta. Only strict
`major.minor.patch` and `major.minor.patch-beta.number` versions are supported.
There is no automatic downgrade, same-version reinstall, polling with the page
closed, PIN persistence or unattended update schedule.

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
needed. The branch holds `channels.json` and `firmware/<sha256>.bin` files; ordinary
source branches do not contain those binaries.

1. Install `.github/workflows/publish-updates.yml`, `tools/prepare_update_channel.py`
   and `tests/test_update_channel.py` on the default branch before enabling
   publication. Beta firmware can stay on its release branch. The workflow always
   runs the publisher from the trusted default branch, not from a release-provided script.
2. Publish a GitHub release/prerelease with its `.bin`, `SHA256SUMS` and
   `BUILD_INFO.txt`, produced by the pinned build pipeline. These are the only
   release attachments needed; keep documentation and previews in the repository.
   Upload all three files before publishing the draft. Keep the tag and `Firmware:`
   version consistent, and list only current attachments in `SHA256SUMS` (not itself).
3. The **Publish browser update channel** workflow verifies the release and
   publishes the appropriate catalog entry. Stable and beta remain separate;
   publishing an older version cannot move either channel backwards.
4. Check that workflow's result after publication. For existing releases or an
   asset-upload race, run it manually with the published tag. Seed both current
   stable and beta tags once when setting up the channel.

The canonical binary is selected using the exact revision in `BUILD_INFO.txt`,
then matched against `SHA256SUMS` and GitHub's asset digest. This also handles a
release retaining a superseded binary. No channel commit occurs on failed checks.
The workflow serializes publications and pushes without force. Repository rules
must permit its token to update only this publication branch as appropriate.

To inspect a release locally without publishing:

```sh
python3 tools/prepare_update_channel.py --tag v1.11.0-beta.1 --output /tmp/clock-update-channel
```

Keep existing channel files when staging a subsequent release. The image name is
its content hash, so cached catalogs still refer to their original bytes. GitHub
CDN caching can delay discovery briefly. A missing catalog, failed download or
checksum mismatch leaves the clock unchanged and offers the local-file path.

## Checks

`tests/test_releases.cjs` compares browser SHA-256 with Node's implementation at
padding boundaries and the maximum firmware size, and checks stable/beta ordering.
`tests/test_update_channel.py` checks channel separation, rollback refusal,
canonical revision selection and rejection of mismatched images/metadata.
`tests/test_web.py` runs the actual embedded page over an insecure HTTP origin,
including PIN failures, corrupt/oversized downloads, channel errors, local-file
fallback and desktop/mobile layouts. The host update harness tests byte identity,
deferred commit, interrupted maximum-size images, multiple files, authorization
and writer failures. These simulations do not replace physical OTA testing.
