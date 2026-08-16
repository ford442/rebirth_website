# Browser demo songs

These `.rbs` files are a curated subset of the ReBirth RB-338 community archive,
hosted locally so the homepage **RbsPlayer** can load previews without cross-origin
fetch issues.

| File                        | Artist / source                | Remote archive path                                   |
| --------------------------- | ------------------------------ | ----------------------------------------------------- |
| `a-taste-of-haste.rbs`      | Action Jackson (official demo) | `By_Source/Rebirth_2.0/Complete/A Taste of Haste.rbs` |
| `orbiting-your-heart.rbs`   | ElekD (official demo)          | `Monthly_Archive/FEBRUARY/Orbiting your Heart.rbs`    |
| `troublemakers.rbs`         | Micromat (official demo)       | `Monthly_Archive/SEPTEMBER/Troublemakers.rbs`         |
| `cavey-3-acid.rbs`          | Cavey                          | `Artists/Cavey/3_acid.rbs`                            |
| `noah-cohn-20.rbs`          | Noah Cohn                      | `Artists/Noah_Cohn_Complete/20.rbs`                   |
| `dj-knightmare-amnesya.rbs` | DJ Knightmare                  | `Artists/DJ Knightmare/amnesya.rbs`                   |
| `metallicon-city.rbs`       | T.G.ViRUS                      | `By_Source/Rebirth_2.0/Complete/Metallicon City.rbs`  |
| `propellerhead-008.rbs`     | Propellerhead                  | `By_Source/Rebirth_2.0/Complete/#008.rbs`             |

The full catalog (thousands of tracks) remains on the external archive; these demos
exist only for reliable in-browser preview and automated testing.

## Refreshing demos

```bash
python3 scripts/sync-demo-songs.py
```

This re-downloads from the remote archive, updates SHA-256 checksums in
`public/rbs-manifest.json`, and overwrites files here. Keep
`scripts/sync-demo-songs.py` aligned with `src/data/demo-songs.ts`.

## Permission / licensing note

ReBirth RB-338 is abandonware (Propellerhead Software / Reason Studios). These
particular song files have been distributed freely by the ReBirth community for
more than two decades. They are used here in a non-commercial archival context.
If you are the author of one of these files and want it removed, please open an
issue on the [GitHub repository](https://github.com/ford442/rebirth_website).
