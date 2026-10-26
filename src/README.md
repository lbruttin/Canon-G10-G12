# Source

The engine, the CHDK module, the desktop tools and the film stocks.

## Layout

| | |
|:--|:--|
| `engine/` | the look, the stock format, the JPEG codec, and the CR2 reader the desktop tool uses. Freestanding C99, integer only on every per-pixel path, no libc. The same files build for the camera and for the desktop. |
| `chdk/` | the camera side: the CHDK module for the G10 and G12. See [chdk/README.md](chdk/README.md). |
| `host/` | `filmdev`, the desktop driver. Develops CR2 frames, builds contact sheets, writes cards, turns Lightroom edits into stocks. |
| `tools/` | `chdktest` (the module's develop on the desktop) and the engine's own cross-checks. |
| `stocks/` | the thirteen films, as plain text. |

The engine was written for the EOS 5D Classic in
[jiijii0/soup-engine](https://github.com/jiijii0/soup-engine), where
Magic Lantern runs it. It is carried here unchanged; some of its comments
still refer to the 5D build it was written alongside.

## Building

```sh
make all        # the desktop tools
make test       # the engine's self-tests
make chdktest   # the module's develop, on the desktop, against camera JPEGs
make arm        # check the engine still builds freestanding for ARM946E-S
```

The module itself is built inside CHDK's source tree, which needs
`arm-none-eabi-gcc`: see [chdk/README.md](chdk/README.md#building).
Everything else needs nothing unusual.

`make chdktest` is the one that matters. It runs `chdk/soup_dev.c`, the
develop exactly as the camera runs it, against a card that returns short
reads and a heap capped at 1 MB. It then checks every frame byte for byte
against a develop done with the whole file in memory. Put G10 or G12 JPEGs
in `samples/powershot/` first.

## Licence

GPL v2. See [../LICENSE](../LICENSE).
