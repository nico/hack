# The benchmark corpus

`images.txt` lists 41 public PNGs that `run.sh` downloads and benchmarks. They're chosen for
performance work: the total time over them should move the way decode time on the web and around
PDFs would. Exotic-format coverage is a different job, done by `png-bench --check` with PngSuite.

## What it should look like

On the web (from HTTP Archive / Web Almanac media statistics, roughly):

- **RGBA8 is the most common PNG**, then 8-bit palette (often pngquant or optimizer output), then
  RGB8. Gray is a few percent; 16-bit and interlaced around a percent or two each.
- **Most files are small** (icons, logos, sprites, UI pieces), but most pixels, and so most decode
  time, are in large screenshots, photos, maps, charts and infographics.

PDFs don't contain PNG files; they hold Flate streams with PNG predictors (the inflate corpus in
SerenityOS covers those). The PNG-shaped images around documents are page renders, scans (gray or
1-bit), and charts and screenshots placed into documents.

The writer matters as much as the content, since it picks the filters, the deflate parameters and
the IDAT chunking. So the set mixes libpng defaults (adaptive filters, many 8 KiB IDATs), optimizer
output (one IDAT, one filter), unfiltered output from screenshot and graphics tools, palette
quantizers, and specialized writers (pdfium's test renderer, Skia, wuffs' test data).

## What's in it

| category | files | share of fastpng's time | share of the png crate's |
|---|---|---|---|
| graphics: maps, charts, diagrams, sprites, pixel art, logos, icons | 20 | 38% | 30% |
| photos, including a cutout on transparency and a 16-bit one | 9 | 23% | 25% |
| documents: newspaper and magazine scans, PDF page renders, equations | 6 | 21% | 26% |
| screenshots: web pages, a browser at Retina resolution, office, desktops | 6 | 19% | 20% |

Sizes run from 16x16 to 3255x5155. No file is more than 14% of the total (the 8-megapixel gray
magazine scan). The small ones barely register in the total; they're there to keep per-image
overhead and their formats exercised, and are best measured on their own with a high `-n`.

| format | files |
|---|---|
| RGBA, 8-bit | 18, one Adam7-interlaced, one APNG |
| RGB, 8-bit | 8, one interlaced |
| palette, 8-bit | 6, one with tRNS |
| palette, 4-bit | 2, one with tRNS |
| gray, 8-bit | 4 |
| gray, 1-bit | 1 |
| gray + alpha, 8-bit | 1 |
| RGB, 16-bit | 1 |

Rows are filtered every which way: all-None (a dozen files, from screenshot and graphics tools),
all-Sub, all-Up, mostly Average (libpng on photos), mostly Paeth, and mixes.

## Where from

- **Wikimedia Commons** (21 files): real-world PNGs with known licenses (listed per file). Found by
  searching for PNGs by kind and size, then reading candidates' headers with HTTP range requests to
  find the rarer formats (interlaced, gray + alpha, low-bit palettes).
- **Git repositories, pinned to a commit**: SerenityOS (screenshot, icons), Skia (mandrill,
  16-bit photo, icon strip), wuffs (test images), Chromium (a CSS sprite sheet), pdfium's test
  corpus (page renders).
- **The Kodak lossless suite** (two photos).

Left out on purpose: very large scans of paintings (10-50 MB; one would dominate), trademark
logos, and anything personal.

## Limits

- Real-world interlaced files are rare and hard to find; the large interlaced one is a logo, the
  photo one is tiny. There's one 16-bit image, matching how rare they are.
- Commons files are fetched at their current version; if one is re-uploaded, `run.sh` says its
  hash changed.
- One finding already: fastpng is slower than the png crate on `clipart-4bit.png` (a 4-bit
  palette whose palette has fewer than 16 entries, so every row takes the per-pixel index check).
