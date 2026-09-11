# CrossPoint library sync

This service exposes byte-for-byte EPUB copies and a compact SHA-256 manifest.
The manifest also contains title, author, series, tag, publication-year, and
visible-character metadata extracted when an EPUB changes. CrossPoint can use
that data to populate its library index without parsing every synchronized book
on the device. Books copied to the SD card manually continue to be indexed on
the device.
The reader derives the endpoint from the configured Calibre URL and uses the
same HTTP Basic credentials:

```
<calibre-base>/crosspoint-sync/v1/manifest.json
```

Install `generate_manifest.py` in `/usr/local/lib/crosspoint-reader`, install
the manifest systemd service and timer, and enable
`crosspoint-calibre-manifest.timer`. Mount `/var/lib/crosspoint-sync` read-only
at the same path in the Nginx container. Include `nginx-location.conf` in the
public Calibre virtual host before its generic `/calibre/` location.

The timer scans `/opt/media/ebooks` every minute. Unchanged files reuse their
previous checksum and metadata based on size and nanosecond mtime; changed files
are hashed, indexed, and copied atomically to `/var/lib/crosspoint-sync/books`.
Removed files are removed from the next manifest and from the asset directory.
