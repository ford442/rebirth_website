# ADR 0001 — Split archive hosting

- **Status:** Accepted
- **Date:** 2026-04-16
- **Deciders:** ReBirth RB-338 archive maintainers

## Context

The site catalogs 5,610 `.rbs` songs and 367 `.rbm` mods, but almost every
byte lives on two hostnames (`test.1ink.us`, `storage.1ink.us`). Eight demo
songs are in-repo. If those hosts lapse, the site becomes an index of files
nobody can obtain — the failure mode a preservation archive exists to prevent.

Cross-origin isolation (COEP/COOP) required by the WASM player also blocks
cross-origin `.rbs` fetches unless the remote sends
`Cross-Origin-Resource-Policy: cross-origin`. Same-origin hosting solves
preservation and isolation together.

Approximate corpus size before dedupe: 5,610 × ~24 KB ≈ 135 MB of songs.
Mods are much larger (many multi-megabyte). Directory listings imply heavy
duplication (`coveragePercent` ≫ 100% vs a 2,000-file historic manifest).

## Options

| Option                             | Same-origin / COEP            | Durability                                                         | Cost                                                       |
| ---------------------------------- | ----------------------------- | ------------------------------------------------------------------ | ---------------------------------------------------------- |
| GitHub Releases as CDN             | No — release assets lack CORP | Medium                                                             | No repo bloat; 2 GB/asset                                  |
| Entire corpus in `public/archive/` | Yes                           | High for forks/clones                                              | Repo + Pages toward the 1 GB soft limit; Git LFS is a trap |
| **Split** (this ADR)               | Yes for the playable core     | High: core in git, tail mirrored off-repo with an integrity bridge | Tractable repo                                             |

## Decision

**Split hosting.**

1. **Playable core** (the songs the in-browser player ships with, plus any
   additional curated previews) lives under `public/archive/rbs-songs/` and is
   served same-origin. This path is COEP-clean.
2. **Full corpus** is mirrored by `scripts/mirror-archive.py` into a
   content-addressed store (`.mirror/blobs/sha256/…`, gitignored). Duplicates
   collapse on SHA-256.
3. **`src/data/archive-integrity.json`** is the canonical preservation record:
   every indexed path, hash when known, byte length, first seen, source URLs,
   and `status` (`local-core` / `mirrored` / `remote` / `dead`).
4. Long-tail publication target is an institutional store (Internet Archive)
   using the integrity manifest as the bridge. That upload is out of band;
   the manifest is what this repo guarantees.

Git LFS is rejected: bandwidth quotas and pointer files break naive
`git clone` mirrors.

## Consequences

- Play and preview of the core set work with COEP enabled and survive host
  lapse for those files.
- The long tail still depends on a remote until a full mirror + IA (or similar)
  deposit is completed. Dead remotes are marked `dead` in the integrity record
  and must not abort a batch.
- CI verifies hashes of `local-core` files on every push. A scheduled workflow
  HEADs remaining external URLs and opens an issue on rot.
- Operators run `python3 scripts/mirror-archive.py --scope all` on a machine
  with disk, then `enrich-rbs-catalog.py --local-mirror .mirror/paths`.
