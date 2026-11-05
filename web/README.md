# TamaPoke web installer

A one-click page that flashes the firmware and loads the sprites from the browser
(Chrome/Edge), with no Arduino or drivers. It uses
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) to flash and **Web
Serial** to push the sprites to the SD with the firmware's `PUT` protocol (the
same one as `tools/send_sd.py`).

## Contents

- `index.html` — the page (flashing + sprite loader).
- `manifest.json` — ESP Web Tools config (points at the firmware).
- `firmware/bootloader.bin` `partitions.bin` `boot_app0.bin` `app.bin` — what the
  installer actually writes, each at its own offset. **This is deliberate**: a
  single image at `0x0` pads the gaps with `0xFF` and so blanks the NVS
  partition at `0x9000`, which is the player's save. `tools/check_installer.py`
  fails the build if anything the manifest ships would land on it.
- `firmware/tamapoke.bin` — the same four merged into one image for flashing a
  **blank** board from the command line. Not in the manifest, because installing
  it over an existing game erases the pet.
- `sprites-<region>.pak` for **kanto, johto, hoenn, sinnoh, unova, kalos, alola,
  galar and paldea** (nine files, ~300 MB in total; the largest is ~43 MB) — the
  sprites bundled (TPAK) **one file per region**. The page sends every pack it
  can find in one go. **Generated** by `tools/pack_bundle.py` (the Galar and
  Paldea packs come from `tools/galar_paldea/`) and **committed** — see
  *Hosting the sprites* below for why they have to be.
- `start-installer.bat` + `serve_nocache.py` — Windows helper: starts a local
  server on `http://localhost:8000` that sends no-cache headers (so Chrome/Edge
  never show a stale page, manifest or firmware after a rebuild) and opens the
  page. Keep both files next to `index.html`; it needs Python on the PATH.

## Regenerate

After changing the firmware or the sprites:

```bash
bash tools/build_web.sh        # recompiles -> firmware/tamapoke.bin AND rebuilds the region .paks
```

## Test locally

Web Serial and ESP Web Tools need a **secure context**: `https://` or
`http://localhost`. To test:

```bash
cd web && python3 serve_nocache.py 8000
# open http://localhost:8000 in Chrome/Edge
```

`python3 -m http.server 8000` also works, but it sends no cache headers, so the
browser may keep showing an old version after a rebuild. On Windows,
double-click `start-installer.bat` instead.

## End-user flow

1. **Install TamaPoke** → flashes the firmware (pick the USB port; tick "Erase
   device" for a fresh board).
2. **Load Sprites** → connects the board, then downloads **every available region
   pack** and copies it to `/mons` on the microSD over USB. It is the slow part
   (Kanto alone is ~40 MB, all nine regions ~300 MB), so leave the tab open;
   re-running it is safe, files are simply written again. Close the step-1
   install dialog first: only one program can use the port at a time. A pack
   that can't be found is skipped and logged.
3. Restart (PWR button) → choose a region and a starter, and play.

Advanced users can instead send a whole `Mon` folder, or individual `.bin` files,
from their own computer.

## Hosting the sprites

**The `.pak` files ARE committed, and that is deliberate.** They have to be
served **same-origin** from GitHub Pages, because **GitHub release assets send
no CORS headers at all** — a browser `fetch()` of one is blocked, however much
nicer it would be to keep ~300 MB out of the repo. Verified with an `Origin`
header: the asset returns `200` and no `access-control-allow-origin`, while
Pages sends `access-control-allow-origin: *`.

This page said the opposite for a long time — "gitignored", "not committed",
"serves Access-Control-Allow-Origin" — while `.gitignore` carried the real
reason and the files were tracked. Acting on this file rather than on the code
untracks them and silently breaks every download button. It is written down
here now so the next person does not have to find out the same way.

```bash
bash tools/build_web.sh   # rebuilds the firmware, the manifest and every .pak
git add web/sprites-*.pak # yes, really
```

The page tries **same-origin first**, then `PAK_RELEASE`. Same-origin is the
path that actually works in a browser; the release fallback is a convenience for
people downloading a bundle by hand, and for local testing you can leave the
`.pak` files in `web/` and run `python3 -m http.server`.

`PAK_RELEASE` at the top of the script block in `index.html` points at the repo;
change it if you fork.

**Why one file per region and not one big one:** all nine together come to about
300 MB, and GitHub's hard per-file limit is 100 MB — a single bundle would be
uncommittable. Splitting also keeps each file small enough to download and
retry, and lets a region be added later without rebuilding the others.
A region whose `.pak` is not on the card shows as locked in the Pokedex chooser
and is kept out of the egg pool, so a partial install is a supported state
rather than a broken one.

All sprites are from PMD SpriteCollab, CC BY-NC (non-commercial sharing with
attribution is allowed); see [`../CREDITS.md`](../CREDITS.md).

## Deploy (GitHub Pages)

1. Repo settings → Pages → serve from `main`, folder `/web` (or move `web/` to
   `docs/`). Pages gives HTTPS automatically.
2. URL ends up at `https://<user>.github.io/<repo>/`.

> **Pages on private repos** needs GitHub Pro/Team. If you make the repo
> **public** to use Pages for free, decide about the sprites first (see above and
> CREDITS).

## Limitations

- Desktop **Chrome/Edge** only (Web Serial isn't in Firefox/Safari).
