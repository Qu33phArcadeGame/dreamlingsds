# Deepdreamlings DS

A Nintendo DS port of Deepdreamlings: walk the dream towns, step into the
water to spin the dream slots (every spin fuses the dreamlings on the reels), collect
cards in your binder, and visit each town's **Dream Generator** to dream a
trading card and zoom into it forever. The dreaming is real: a small neural
network runs on the DS's own ARM9 CPU.

## Playing

| | |
| --- | --- |
| D-pad | walk (hold **B** to run) |
| A | talk / confirm |
| START | binder |
| SELECT | save |

* **Dream Core**: the lake is the encounter zone. Step into it to spin the slots.
* **Waking Woods**: little ponds are scattered all over the woods. Walking
  through a pond can start an encounter at random.
* **Area 3**: through the south hall of the woods.
* Talk to Bob in the house in Dream Core.
* Dream Core's slots have 2 reels (you get 2-part Fusions); from the Waking
  Woods on there are 3 reels, so that's where Tri-beings start. Change it with
  `slots` in `AREAS` (`source/common.c`).
* Slot rarity per reel: 50% common, 35% rare, 15% holo.
* In the binder you can set a companion (it circles you in town) or fuse 2-4
  fusions into a bigger one (Tri-being, Deepdreamling...).

### The Dream Generator

Walk into the purple building with the eye. Your newest card pops up: the
creature in front of its dream photo, in the card frame.

* **Upright** (default): card on the top screen, controls on the touch screen.
  Tap **Rotate** (or press **R**) for **Book mode**: hold the DS sideways like
  a book and the card fills a whole screen. Left-handed book mode is in Settings.
* **Zoom** falls into the card along a straight, square or triangle path,
  dreaming as it goes. The first frame is the plain card (no starting dream).
* Settings has the same knobs as the website: zoom factor, steps per frame,
  learning rate, movement range, frames per side, keep sharp,
  layers (lo / mid / hi), focus (one branch of the Inception block) and
  dream size (40x50 fast, 80x100 detailed). Presets are tuned for the DS.
* Playback is a zoomerang (in, then back out) with separate in / out speeds.
* The recipe printed on the card follows the frame you're watching, one zoom
  run at a time.
* **Keep** saves the card to the SD card (`dreamlings/cards/`); **Gallery**
  loads it back. **GIF** writes the animated card to `dreamlings/gifs/`.

Each town dreams on its own model, like the website:

| Town | Website model | DS model layers (lo / mid / hi) |
| --- | --- | --- |
| Dream Core | Inception v1 | conv2d2 / mixed3b / mixed4c |
| Waking Woods | Inception v2 | Conv2d_2c / Mixed_3c / Mixed_4c |
| Area 3 | Inception v3 | conv_4 / mixed2 / mixed5 |

## The DS dream models

Inception is far too big for a DS (and has no hope of running in 4 MB of RAM),
so each town gets a tiny network of its own (5 conv layers, ~22k weights,
fixed-point) that was *taught* by the real Inception model:

1. `tools/train_dream_models.py` makes ~3000 pictures from the game's own art,
   runs the website's Inception model on them (`tools/teachers.py` runs the
   TensorFlow.js models in plain numpy), and records three of its layers.
2. The tiny network learns to predict those layers (`tools/student.py`).
3. It then dreams with itself, shows the dreams to Inception again and
   retrains on them, so it agrees with Inception on dream-like pictures too.
4. `tools/export_dream_models.py` turns it into fixed-point tables in
   `source/dream_models.c`, which `source/dreamnet.c` runs on the DS
   (16-bit multiply-accumulates, gradient ascent, jitter, octaves, zoom).

To add a model (VGG16, ...), add a teacher to `teachers.py`, an entry in
`MODELS` in `train_dream_models.py`, retrain and export, then point an area
at it in `source/common.c` (`AREAS`).

Rebuild everything from the website repo:

```bash
python3 tools/convert_assets.py --web ../DeepdreamlingsFusion --frame tools/cardframe.png
python3 tools/train_dream_models.py --web ../DeepdreamlingsFusion   # ~30 min, numpy only
python3 tools/export_dream_models.py
```

Only numpy, scipy and Pillow are needed.

## Building

Push to GitHub and the **Build Dreamlings DS** workflow builds `dreamlings.nds`
(download it from the run's artifacts). Locally, with
[BlocksDS](https://blocksds.skylyrac.net) installed: `make`.

`make pc` builds a headless test version that plays a script of button
presses and saves screenshots (see `source/platform_pc.c`).

## Saving

Saves need an SD card: the DSi/3DS SD slot, a flashcart, or an emulator with
an SD card image (melonDS: enable DLDI / DSi SD). Without one the game still
plays, it just can't save.

## Speed

Dreaming is slow on purpose-built 2004 hardware. On an original DS a dream
step takes roughly a second at 80x100 (about a quarter of that at 40x50). A
DSi or 3DS runs twice as fast and has 16 MB of RAM, so cards can hold many
more frames. The generator shows its own seconds-per-frame estimate.
