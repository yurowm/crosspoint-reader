# CrossPoint library sync

This service exposes byte-for-byte EPUB copies and a compact SHA-256 manifest.
The reader derives the endpoint from the configured Calibre URL and uses the
same HTTP Basic credentials:

```
<calibre-base>/crosspoint-sync/v1/manifest.json
```

Install `generate_manifest.py` in `/usr/local/lib/crosspoint-reader`, install
the three systemd units, and enable both `crosspoint-calibre-sync.service` and
`crosspoint-calibre-manifest.timer`. Include `nginx-location.conf` in the
public Calibre virtual host before its generic `/calibre/` location.

The timer scans `/opt/media/ebooks` every minute. Unchanged files reuse their
previous checksum based on size and nanosecond mtime; changed files are hashed
and copied atomically to `/var/lib/crosspoint-sync/books`. Removed files are
removed from the next manifest and from the asset directory.
