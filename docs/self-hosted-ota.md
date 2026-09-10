# Self-hosted OTA releases

Production firmware checks the compact release manifest at:

```text
https://server.yurowm.in/crosspoint-reader/latest.json
```

The manifest deliberately uses the subset of the GitHub Releases JSON format
already understood by `ReleaseJsonParser`: `tag_name`, plus an `assets` array
containing `name`, `browser_download_url`, and `size`. An additional `sha256`
field is published for external verification and ignored by older parsers.

Release files are immutable and stored below
`/var/www/crosspoint-reader/releases/<version>/`. The `latest.json` manifest is
replaced atomically only after all firmware files have been staged.

## Publish a release

Build the required production environments first, then pass one or more
`model=path` mappings:

```bash
bin/publish-ota-release 1.5.0-25 \
  x4pro=.pio/build/x4pro-gh_release/firmware.bin
```

Multiple models can be published in the same release without changing the
manifest format:

```bash
bin/publish-ota-release 1.5.0-26 \
  x4pro=.pio/build/x4pro-gh_release/firmware.bin \
  x4=.pio/build/gh_release/firmware.bin
```

The X4 asset keeps the historical name `firmware.bin`; other models use
`firmware-<model>.bin`, matching `FirmwareBoardTag` and `OtaUpdater`.

The web server includes `deploy/nginx/crosspoint-reader-ota.location.conf` in
the HTTPS virtual host for `server.yurowm.in`.
