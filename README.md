# NOCTIS

A retro gothic first-person shooter written in plain C99/C11 with **no
external libraries**: just libc and libm.

- **Graphics:** a software raycaster that talks to the X server over the raw
  X11 wire protocol (Unix socket, MIT-MAGIC-COOKIE auth, MIT-SHM with a
  PutImage fallback). It does not use Xlib or XCB.
- **Audio:** a client for the PulseAudio native protocol, which works with
  PulseAudio and pipewire-pulse. It feeds a software mixer with positional
  sound, an organ/bell music sequencer and a cathedral reverb. If no server
  is available, the game runs silent.
- **Art:** every texture, sprite, weapon, the sky, the font and every sound
  is generated procedurally at startup. There are no asset files.

Features: 256-colour palette with Doom-style light colormaps, flickering
per-cell dynamic lighting, distance fog, sliding doors, open-air courtyards
under a moonlit (or blood-red) sky with rain and lightning, translucent
wraiths, particles, an automap, and three chapters ending with a boss.

## Build & run

```sh
nix develop          # dev shell: gcc, clang, clangd, make, gdb, valgrind, imagemagick
make run

nix run              # or build and run straight from the flake
```

Without Nix, any C compiler works: `make && ./noctis`.

Options: `--level N` (start at chapter N), `--window WxH`, `--mute`.

## Controls

| Action | Keys |
|---|---|
| Move / strafe | W A S D (arrow keys move and turn) |
| Look | Mouse (click the window to capture it) |
| Run | Shift |
| Fire | Left mouse / Ctrl / F |
| Use (doors, seals) | E / Space / Right mouse |
| Weapons | 1 dagger, 2 crossbow, 3 blunderbuss, Q / wheel to cycle |
| Automap | Tab |
| Music on/off | M |
| Menu | Esc |
| Fullscreen | F11 / Alt+Enter |

## Development helpers

```sh
./noctis --validate                        # check every chapter can be finished
./noctis --shot out.ppm --level 2 --frames 60 [--pos X Y ANGLE] [--weapon N] [--fire K] [--use] [--map]
make shots                                 # headless screenshots in build/
NOCTIS_FPS=1 ./noctis                      # print the frame rate
NOCTIS_NO_SHM=1 / NOCTIS_NO_AUDIO=1        # force the fallbacks
```

## Source layout

| File | Contents |
|---|---|
| `src/x11.c` | X11 wire protocol: connect, auth, window, input, SHM presentation |
| `src/audio.c` | PulseAudio protocol, mixer, music sequencer, reverb |
| `src/sfx.c` | Procedural sound synthesis |
| `src/art.c` | Palette, colormaps, procedural textures, sprites, font |
| `src/render.c` | Raycaster, floor/sky casting, sprites, HUD, automap |
| `src/game.c` | Player, weapons, doors, AI and flow-field pathfinding, lighting |
| `src/levels.c` | ASCII maps for the three chapters |
| `src/main.c` | Main loop, menus, headless tools |
