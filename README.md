# Steam Linux Runtime 4 + Nix: a build setup proof of concept

This repository shows how to build a native Linux game with **Nix** so that
the result runs on **Steam** through the **Steam Linux Runtime 4.0**
(`steamrt4`, based on Debian 13), and how to make local development behave
like the Steam release.

The game here (a small SDL3 shooter, see [the bottom](#the-game)) only serves
as a test case. The interesting part is `flake.nix`.

## Goals

- **Portable release binary.** The shipped executable depends only on
  libraries that the Steam Linux Runtime provides (`libSDL3.so.0`,
  `libm.so.6`, `libc.so.6`). It has no RPATH and no `/nix/store` paths, and
  Nix is not needed to run it.
- **Built with Valve's toolchain.** Compilation uses the steamrt4 **SDK**: its
  gcc, glibc headers, SDL3 headers and pkg-config. Nix's compiler and
  libraries are never used for the release.
- **Local dev matches the release.** `make` in the dev shell compiles with the
  same SDK, and `make run` starts the game inside the same container Steam
  uses (pressure-vessel + the steamrt4 platform).
- **Pinned and reproducible.** The runtime version is pinned in the flake,
  and the hashes come from Valve's published `SHA256SUMS`.

## Quick start

```sh
nix develop          # dev shell: compiler and pkg-config run inside the steamrt4 SDK
make run             # build with the SDK, play inside the Steam Linux Runtime 4
make run ARGS="--level 2"

nix build            # release build: result/ is the Steam depot root
nix run              # build the release and play it inside the runtime
nix flake check      # build the release and check its dependencies
```

## How it works

```
                    ┌──────────────── Nix ────────────────┐
  Valve images ───▶ │ fetchurl (pinned, SHA256SUMS hash)  │
                    │   ├─ SDK sysroot  (unpacked, 3.4 GB) │
                    │   └─ SteamLinuxRuntime_4 (launcher)  │
                    └─────────────────────────────────────┘
                                  │
     steamrt4-sdk  ◀──────────────┘  bubblewrap: SDK as /, project dir bound in
         │                           no network, clean environment
         ▼
     make CC="steamrt4-sdk gcc" PKG_CONFIG="steamrt4-sdk pkg-config"
         │
         ▼
     noctis  (ELF, interpreter /lib64/ld-linux-x86-64.so.2,
              NEEDED libSDL3.so.0 libm.so.6 libc.so.6)
         │
         ├──▶ nix build: checked, stripped, copied to result/  ──▶ Steam depot
         └──▶ steamrt4-run: pressure-vessel container, same as Steam
```

### 1. The SDK as a sandboxed toolchain

Valve publishes the steamrt4 SDK as a sysroot tarball. The flake fetches it at
a pinned version and unpacks it into the Nix store once (it strips setuid bits
and ownership, which the store can't hold).

`steamrt4-sdk CMD...` runs a command inside that sysroot using **bubblewrap**:

- every top-level directory of the sysroot is bind-mounted read-only at `/`
- the project directory (`$STEAMRT4_SDK_BIND`, default `$PWD`) is bound
  read-write at its real path, so relative and absolute paths both work
- the environment is cleared, and the network and other namespaces are
  unshared

Because `CC` and `PKG_CONFIG` are just commands, the Makefile needs no special
knowledge of the SDK:

```make
CC         = steamrt4-sdk gcc
PKG_CONFIG = steamrt4-sdk pkg-config
```

The same wrapper runs inside the Nix build sandbox for the release build, so
the dev shell and `nix build` use the same compiler.

### 2. The release derivation

`packages.release` (the default package) is a `stdenvNoCC` derivation, so no
Nix compiler is involved. It:

1. runs `make` through `steamrt4-sdk`
2. **checks** the binary, failing the build if:
   - the ELF interpreter isn't the standard one
   - any `NEEDED` library is outside the allowed list
   - it has an `RPATH`/`RUNPATH`
   - it contains the string `/nix/store`
3. runs headless tests inside the SDK (`--validate`, a `--shot` screenshot)
4. splits debug info with the SDK's `objcopy`/`strip` into a `debug` output
   (`noctis.debug`, linked by `.gnu_debuglink`)
5. installs **only** the binary into `$out`

`dontFixup = true` stops Nix from patchelf-ing or re-stripping the binary,
and `allowedReferences = [ ]` makes Nix itself reject any store reference.

The source is filtered to `Makefile` and `src/`, so editing the flake or the
README doesn't trigger a rebuild.

### 3. Running in the real runtime

`steamrt4-run CMD...` launches a command with Valve's `SteamLinuxRuntime_4/run`.
This is pressure-vessel, the same container launcher Steam uses for games that
target this runtime. It imports the host's graphics drivers and provides
steamrt4's libraries, including SDL3.

Two adjustments make it work from Nix:

- **FHS host.** pressure-vessel expects a conventional `/usr` layout on the
  host (for example `/usr/bin/true`, `ld.so.cache` and GL/Vulkan drivers under
  `/usr/lib`). On NixOS, `steamrt4-run` is a `buildFHSEnv` that provides one,
  the same way nixpkgs' Steam package does. On other distros this layer is
  thin.
- **Writable runtime.** pressure-vessel writes lock files inside the runtime
  directory, so it can't run from the read-only store. The first run copies
  the runtime (~700 MB) to `~/.cache/steamrt4/<store-hash>-…`. Changing the
  pinned version produces a new directory. Delete old ones by hand.

`make run` uses `$(RUNNER)`, which the dev shell sets to `steamrt4-run`, and
`nix run` wraps the release binary the same way.

## Flake outputs

| Output | Description |
|---|---|
| `packages.release` (default) | Built in the steamrt4 SDK. `$out/noctis` is the depot root; the `debug` output holds `lib/debug/noctis.debug` |
| `packages.play`, `apps.default` | The release, launched inside the Steam Linux Runtime 4 |
| `packages.steamrt4-sdk` | `steamrt4-sdk CMD...`: run any SDK tool (gcc, gdb, readelf, objdump, ...) |
| `packages.steamrt4-run` | `steamrt4-run CMD...`: run a command inside the runtime container |
| `packages.native`, `apps.native` | A normal Nix build against nixpkgs' SDL3 (not release-like) |
| `devShells.default` | `CC`, `PKG_CONFIG` and `RUNNER` point at the SDK and runtime. Uses `mkShellNoCC`, so no Nix compiler can sneak in |
| `devShells.native` | Nix gcc/clang, SDL3, gdb, valgrind, clangd, bear: for sanitizers and tooling that are easier outside the container |
| `checks.release` | The release build, including its dependency checks |

Systems: `x86_64-linux` (tested) and `aarch64-linux` (uses Valve's arm64
images; it evaluates but hasn't been built or run).

## Development workflow

```sh
nix develop
make                              # compile with the SDK
make run                          # play in the runtime container
make shots                        # headless screenshots → build/shot-*.ppm
steamrt4-sdk ./noctis --validate  # headless binaries run directly in the SDK
steamrt4-sdk gdb ./noctis         # SDK tools
steamrt4-sdk readelf -d noctis    # inspect dependencies
```

The binary built in the dev shell is the same kind of binary as the release.
On NixOS it can't run on the host directly (there is no `libSDL3.so.0`
there); that is intentional. Run it through `steamrt4-run` or `steamrt4-sdk`.

For tools that work better against a native build (valgrind, ASan/UBSan,
clangd with `bear -- make`), use `nix develop .#native`.

The Makefile itself is plain: outside Nix, any C compiler with SDL3
development files works (`make && ./noctis`).

## Shipping on Steam

1. `nix build` and upload the contents of `result/` as the Linux depot
   (for example with `steamcmd`, `ContentRoot` pointing at `result/`).
2. Set the launch executable to `noctis`.
3. In Steamworks, select **Steam Linux Runtime 4.0** as the Linux runtime for
   the app, so Steam starts it in the same container `steamrt4-run` uses.

Keep `result-debug/lib/debug/noctis.debug` (`nix build .#release.debug`)
for symbolizing crash reports.

## Updating the runtime

In `flake.nix`, change `steamrt.version` to a directory listed at
<https://repo.steampowered.com/steamrt4/images/>, and copy the new hashes for
the SDK sysroot and `SteamLinuxRuntime_4*.tar.xz` from that version's
`SHA256SUMS` file. Then run `nix flake check`.

## Requirements and caveats

- **Disk:** the SDK sysroot uses about 3.4 GB in the Nix store (1 GB
  download), and the runtime cache about 700 MB.
- **User namespaces:** bubblewrap needs unprivileged user namespaces, both on
  the host and inside the Nix build sandbox (including on remote builders and
  CI).
- **pressure-vessel warnings:** on NixOS it prints harmless warnings about the
  missing i386 libraries and `getent`.
- **Platform runtime vs SDK:** the release is compiled against the SDK of the
  same runtime version used for `steamrt4-run`. Steam updates the runtime on
  players' machines on its own schedule. Valve aims to keep each major runtime
  version backward compatible, so building against a slightly older SDK than
  the one players have is the safe direction.

## The game

**NOCTIS** is a tiny retro gothic first-person shooter in plain C11 on SDL3.
A software raycaster draws a 480x270 8-bit framebuffer, a software mixer
handles music and sound, and every texture, sprite and sound is generated
procedurally at startup. It has three chapters.

WASD to move, mouse to look (click to capture), LMB/Ctrl to fire, E/Space to
use, 1–3 or Q/wheel for weapons, Tab for the map, Esc for the menu, F11 for
fullscreen. Options: `--level N`, `--window WxH`, `--mute`. Set `NOCTIS_FPS=1`
to print the frame rate, `NOCTIS_NO_AUDIO=1` to run silent.

Source: `src/sdl.c` (platform), `src/audio.c` + `src/sfx.c` (sound),
`src/art.c` (procedural art), `src/render.c` (raycaster), `src/game.c`
(gameplay), `src/levels.c` (maps), `src/main.c` (main loop, headless tools).
