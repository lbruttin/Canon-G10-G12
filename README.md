<div align="center">

# 🎞️ Soup Engine for PowerShot <sub><sup>experimental</sup></sub>

### Film looks, in the camera, on a Canon PowerShot G10 or G12.

<br>

`experimental` &nbsp;·&nbsp; `GPL v2` &nbsp;·&nbsp; `13 film stocks` &nbsp;·&nbsp; `CHDK module`

</div>

<br>

Open **Soup Engine** from CHDK's Tools menu and a list of film stocks
fills the screen. Pick one, switch AUTO on, and shoot: every photo is
developed with the look a minute or three later, in the background.

By default the original is kept and the film version goes beside it:

```
A/DCIM/100CANON/IMG_1234.JPG     your photo, untouched
A/FILM/100CANON/IMG_1234.JPG     the same photo with the look on it
```

Or set **ORIG: REPLACE**, and the film version takes the original's
place. Then it's the only version on the card, and the camera's own
playback shows it. The original is only removed once the film version
has been fully written.

<br>

![all thirteen film looks on one frame](doc/stocks.jpg)

<div align="center"><sub>
All thirteen looks on one frame.
</sub></div>

<br>

> ⚠️ **Experimental.** It runs on a G10 (firmware 1.04A, CHDK 1.6.1):
> AUTO, REPLACE and both sizes work, and a frame developed on the camera
> came out byte-identical to the same develop on the desktop. That is one
> camera so far, and the G12 is untested. Please open an issue with what
> happens on yours.

> ⚖️ **No warranty.** Like all GPL software this comes with none. You
> install it at your own risk. See sections 11 and 12 of the
> [licence](LICENSE).

> 🎨 **The looks are still being tweaked.** The names are shorthand for
> the films they're based on, not exact copies. All trademarks belong to
> their owners.

<br>

---

<br>

## 📦 Installing

You need a **Canon PowerShot G10 or G12** and an SD card.

<br>

### 1️⃣ &nbsp; Install CHDK

> ### 👉 [chdk.fandom.com/wiki/CHDK](https://chdk.fandom.com/wiki/CHDK)

CHDK is the open-source add-on firmware for PowerShots. It runs from the
card and leaves the camera's own firmware alone. Follow their guide for
your model and firmware version, then check that CHDK starts and its menu
opens before going further.

<br>

### 2️⃣ &nbsp; Add Soup Engine to the card

| on the card | what it is | where it comes from |
|:--|:--|:--|
| ⚙️ `CHDK/MODULES/SOUP.FLT` | the module | build it, see [`src/chdk`](src/chdk/README.md#building) |
| 🎞️ `CHDK/FILM/` | the film stocks | copy [`card/CHDK/FILM`](card/CHDK/FILM) from this repo |

There's no prebuilt release yet, so for now the module has to be built
from source alongside CHDK.

<br>

### ✅ &nbsp; Open it

In CHDK's menu: **Tools → Soup Engine**.

<br>

---

<br>

## 📷 Using it

| button | what it does |
|:--|:--|
| ↕️ **Up / Down** | choose a film |
| ↔️ **Left / Right** | half size or full size |
| ✅ **`SET`** | develop the newest photo |
| 📚 **`DISP`** | develop every photo in the folder that has no film copy yet |
| 🗑️ **Trash** | AUTO on or off: develop each new photo after it's taken |
| 🔍 **Zoom** | ORIG: KEEP the original, or REPLACE it with the film version |
| ↩️ **`MENU`** | close the picker. AUTO keeps running. |

Your choices survive switching the camera off. CHDK doesn't load modules
by itself, though: after switching on, open Soup Engine once so AUTO
starts.

Developing runs in the background, so you can keep shooting. While a
photo develops, **`FILM 37%`** shows in the top right corner. On a G10 a
photo takes about 50 seconds at half size and about 3 minutes at full
size. With REPLACE, use full size, or the film version keeps only a
quarter of the pixels.

<br>

> ### ⚠️ Turn *My Colors* off
>
> The looks start from the camera's own JPEG. With My Colors set to B/W
> or Sepia, every look comes out that way.

<br>

Shooting **RAW only** gives Soup Engine nothing to work from. Shoot JPEG,
or RAW + JPEG.

<br>

---

<br>

## 🎞️ The films

Thirteen, still under development.

They're small text files in `CHDK/FILM` on the card, so you can read them,
change them, or write your own. [`src/stocks`](src/stocks/README.md)
explains the format.

<br>

<div align="center">

### 🎨 [Soup Stocks](https://jiijii0.github.io/soup-stocks/)

**A web editor for the film stocks.**

Drop in a photo, move sliders, download a `.fs`, put it on the card.

</div>

<br>

---

<br>

### Starting CHDK by itself

CHDK normally has to be started from the playback menu (**Firm Update…**)
each time the camera is switched on. The G10 can start it by itself from
a **FAT16** card, which caps the card at 2 GB (about 500 JPEGs). Format the
card as FAT16, put CHDK on it, run **Miscellaneous stuff → SD Card → Make
Card Bootable** once, and slide the card's tab to **LOCK**. Photos still
save with the tab locked; CHDK takes care of that.

<br>

---

<br>

## 🧹 Removing it

Delete `CHDK/MODULES/SOUP.FLT`. The film copies in `A/FILM` are ordinary
JPEGs; keep them or delete them. Photos developed with REPLACE have no
original left.

> 🆘 **If the camera ever misbehaves:** take the battery out, put the
> card in a computer, delete `SOUP.FLT`. CHDK and the camera carry on
> without it.

<br>

---

<br>

## ❤️ Thanks

Soup Engine was written for the **Canon EOS 5D Classic** by
[@jiijii0](https://github.com/jiijii0), inside Magic Lantern:
[jiijii0/soup-engine](https://github.com/jiijii0/soup-engine). The film
looks, the stock format and the whole engine are that project's. This
repository is the PowerShot port, as a CHDK module.

It runs on [CHDK](https://chdk.fandom.com/wiki/CHDK), which makes running
code on a PowerShot possible at all. The original exists thanks to
[Magic Lantern](https://magiclantern.fm). This port isn't either
project's, so please bring problems with it here, not to them.

<br>

## 📄 Source and licence

All of it is in [`src/`](src). The camera side is in
[`src/chdk/`](src/chdk), with what's tested, what's missing and how to
build it.

GPL v2. See [LICENSE](LICENSE). Distributed **without any warranty**,
including the implied warranties of merchantability and fitness for a
particular purpose (sections 11 and 12).
