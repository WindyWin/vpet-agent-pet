# Pack sources

Open **Settings → Pet** (or **Plugins**) → **Browse pack sources…**. The official
[gallery](https://github.com/WindyWin/agent-pet-gallery) is configured by default.
Choose **Refresh** to load its current release catalog, then select a pack for its
preview, author, licence, compatibility, and any integration setup notes.

**Install…** or **Update…** downloads the selected version. The app checks its exact
byte count and SHA-256, validates the archive using the local import pipeline, and
checks its identity and metadata against the catalog. Review the complete terms
and choose **Accept terms and install**. Restart Agent Pet to apply the queued
change. An installed version is preserved if download, validation or consent fails.
**Cancel download** or closing the browser discards the temporary download.

**Remove pack…** queues removal for the next start. Removing the selected pet
switches to VPet; removing a plugin clears its enabled preference. Newly installed
plugins are not enabled automatically. Enable them separately in Settings, and
follow setup notes for any external extension or bridge: installing data does not
connect an integration.

## Third-party sources and direct links

Use **Add source…** with an HTTPS catalog URL. The warning explains that the source
operator controls its contents and updates. Third-party sources are labelled in
the source selector. A checksum proves agreement with that catalog, not authorship
or redistribution permission. Metadata, descriptions and setup notes are plain text.
Every installation, including updates and changes of source, requires consent.

**Install from URL…** accepts an HTTPS `.agentpet` link without a catalog. It warns
that this download has no expected digest. The archive limits, compatibility checks
and full terms confirmation still apply. Local file imports remain available.

**Remove source** removes a third-party source from the list; it never uninstalls
its packs. The built-in official source cannot be removed, but no refresh runs in
the background. Selecting a pack fetches its PNG preview. Browsing a cached catalog
works offline; previews may be unavailable. A failed refresh retains the last
valid catalog. Packs missing from a newer catalog remain installed and can still be
removed with Settings' existing **Remove installed pack…** control.

Configuration and catalog caches live under `<data>/sources/`, in the same per-user
application data location as local packs. A read-only or package-managed application
install does not prevent installing packs. The browser offers manual updates;
background updates and persisted automatic-update consent belong to phase 6.

## Publishing a source

Host an HTTPS `index.json` matching the example below. Use `schema_version: 1` and
one entry per `(kind, id)`. Publish immutable, versioned `.agentpet` archives and
PNG previews, then update the index. No app release is needed: users choose Refresh.
The official index is the gallery's latest release asset:
`https://github.com/WindyWin/agent-pet-gallery/releases/latest/download/index.json`.

```json
{
  "schema_version": 1,
  "packs": [{
    "kind": "pet",
    "id": "example-cat",
    "version": "1.0.0",
    "name": "Example Cat",
    "description": "A small companion.",
    "author": "Example artist",
    "license": "CC0-1.0",
    "schema": 2,
    "minAppVersion": "0.17.0",
    "preview": "https://example.org/releases/1.0.0/cat.png",
    "download": "https://example.org/releases/1.0.0/cat.agentpet",
    "bytes": 12345,
    "sha256": "replace-with-the-64-lowercase-hexadecimal-sha256-digest"
  }]
}
```

The size/digest above are placeholders. Use the exact archive byte count and SHA-256.
`minAppVersion` must name an app release that actually supports the pack's features;
the example is syntax, not a compatibility claim. Identity, version, author, name,
licence, schema, minimum version and plugin target must agree with `pack.json`.

Plugin entries use `kind: "plugin"` and require `pet`, the target pet id. Optional
`setup` text (up to 4096 characters) explains external dependencies. A missing target
pet disables installation until that pet is installed and the application restarted.
No field executes code, enables plugins, or registers external hooks.

Indexes are capped at 2 MiB and 1000 entries. Unknown fields, duplicate identities,
invalid versions, non-HTTPS URLs, credentials, URL fragments and missing digests are
rejected without replacing the cache. Description and setup text are capped at 4096
characters; names/licences at 64 and authors at 128. Source URLs are at most 2048
characters, and at most 32 sources including the official source may be configured.
Previews must be PNG, at most 4 MiB and 512 × 512 pixels. Archive downloads are capped
at 544 MiB; the importer enforces the stricter per-kind extracted-data limits.
HTTPS redirects are allowed up to five hops; credentials and insecure redirects are
rejected. Idle requests time out after 30 seconds, with a ten-minute transfer limit.
