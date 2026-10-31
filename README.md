# Splinter Cell: Blacklist for PS5

An unofficial native PS5 homebrew port of the Xbox 360 version of *Tom Clancy's Splinter Cell:
Blacklist*, made with static recompilation: the game's PowerPC code is translated to C++ by
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the PS5, with the Xbox 360 GPU
emulated on the PS5's GPU through Vulkan (RADV).

The app installs as `PPSA90827` and opens a launcher where you pick a disc (Disc 1: Campaign Part 1
& Multiplayer, Disc 2: Campaign Part 2 & HD Texture Pack) and change graphics settings.

## No game files are included: bring your own discs

> [!IMPORTANT]
> This repository contains **no game data of any kind**: no disc images, no game files, no
> executables, no recompiled game code and no copyrighted assets of the game. None will be provided,
> shared or sent, so please don't ask.
>
> To build and play it you need **your own legally obtained copy** of the Xbox 360 game (both
> discs) and must dump the files from it yourself. The game's code is translated from your own
> disc's `default.xex` on your own computer, and the game data is copied from your own discs to
> your own PS5.
>
> Splinter Cell and Splinter Cell: Blacklist are trademarks of Ubisoft Entertainment. This project
> is not affiliated with, authorized or endorsed by Ubisoft, Microsoft or Sony.

## What you need

- A PS5 that can run homebrew apps installed to `/data/homebrew`, and a way to copy files to it
  (FTP or a file manager).
- A Mac (Apple Silicon or Intel) or Linux PC with [Docker](https://www.docker.com/) and git. On a
  Mac the build runs in an x86-64 Linux container under Rosetta.
- Plenty of free disk space for Docker (tens of GB for the build), plus room for the game.
- Your own Xbox 360 discs of the game, extracted to plain files (for example with
  [extract-xiso](https://github.com/XboxDev/extract-xiso) from your own disc images).

## Building

```bash
git clone --recursive https://github.com/hexreaper711/splinter-cell-blacklist-ps5.git
```

```bash
cd splinter-cell-blacklist-ps5
```

Copy `default.xex` from **your own** extracted disc 1 to `disc-files/disc1/default.xex`
(see [disc-files/README.md](disc-files/README.md)). Disc 2 isn't needed for the build: its
`default.xex` has the same game code as disc 1's. Both discs go on the PS5 afterwards
([Installing](#installing)). Then:

```bash
./build.sh
```

It does every step and skips the ones already done:

1. builds the Docker image (`docker/Dockerfile`);
2. builds the PS5 payload SDK, RADV and the app runtime in
   [ps5-plume-triangle](https://github.com/Nazky/ps5-plume-triangle) (once);
3. builds the Linux `rexglue` tool from the SDK (once);
4. translates the game's code from your `default.xex` (once, 10-15 minutes);
5. builds and signs the PS5 app;
6. packs it into **`dist/splinter-cell-blacklist-ps5-<version>.zip`**.

The first run takes a few hours on a Mac, because the dependencies are built from source. Later
runs only rebuild what changed. `./build.sh --clean` rebuilds the game (not the dependencies).

### Versions

The zip is named after the version, which comes from git: on a commit tagged `v0.1.0` it's
`splinter-cell-blacklist-ps5-v0.1.0.zip`, three commits later `...-v0.1.0-3-g<commit>.zip`, and
`-modified` is added when the files differ from the commit. `./build.sh --version 0.2.0` sets
the version itself. Inside the app, `VERSION.txt` records it, and an `x.y.z` version also
becomes the app's content version in `sce_sys/param.json`.

```bash
git tag v0.1.0 && ./build.sh
```

Older zips in `dist/` are kept.

### Your discs in the package

`./build.sh --with-discs` also copies your own discs from `disc-files/disc1` and `disc2` into
the app folder `dist/PPSA90827`, so it's ready to copy to your PS5 as it is. The zip never
contains them.

`./dev.sh` opens a shell in the build container, if you want to run steps yourself.

## Installing

1. Unzip `dist/splinter-cell-blacklist-ps5-<version>.zip` and copy the `PPSA90827` folder in it
   to `/data/homebrew/` on the PS5.
2. Copy **your own** extracted discs into it, each disc's whole root (with its `default.xex`),
   leaving out `$SystemUpdate`:

   ```
   /data/homebrew/PPSA90827/game/disc1/   <- Disc 1 (Campaign Part 1 & Multiplayer)
   /data/homebrew/PPSA90827/game/disc2/   <- Disc 2 (Campaign Part 2 & HD Texture Pack)
   ```

   - **Disc 1** alone is enough to start the game.
   - **Disc 2** is needed for the second half of the campaign. It also holds the HD Texture
     Pack (`Content/` on the disc, about 3 GB), which the launcher installs once when you start
     a disc.

   The launcher shows which discs it found.
3. Start the app from your homebrew launcher.

If your PS5 runs a web file manager on port 8888, `tools/ps5_upload.py` can upload the folders
from your computer:

```bash
PS5_HOST=<your PS5's IP>:8888 python3 tools/ps5_upload.py dist/PPSA90827 /data/homebrew --overwrite
```

## Playing

- **Launcher:** up/down picks a disc, **Cross** starts it. **L1/R1** opens the settings, where
  left/right or Cross changes a setting and **Square** restores the recommended ones.
- **Back to the launcher:** hold **OPTIONS + touchpad** for 2 seconds in the game.
- **When the story asks for the other disc,** the app switches to it if it's installed
  (not tested yet).
- Saves are kept in `PPSA90827/UserData/` and are shared by both discs.
- Logs go to `PPSA90827/logs/`. **L3 + R3** in the game writes a report of one frame's GPU work
  to the log, for debugging (`tools/ps5_gpu_frame.py` reads it).

## Status

Work in progress. The campaign starts and plays; known issues:

- Movement doesn't feel fully smooth yet (frame pacing is being worked on).
- Higher resolutions (1440p, 4K) are experimental.
- Switching discs in the middle of the story hasn't been tested yet.
- Online features aren't supported.

## How it fits together

| Path | What it is |
| --- | --- |
| `src/` | The app: disc launcher, settings, HD Texture Pack installer, disc swapping |
| `ps5/` | PS5 build: CMake project, toolchain, link and packaging script, launcher assets |
| `blacklist_manifest.toml` | What `rexglue codegen` translates from `default.xex` |
| `thirdparty/rexglue-sdk` | The ReXGlue SDK with the PS5 runtime and this game's fixes, in this repository ([history](thirdparty/REXGLUE-SDK.md)); its libraries are submodules |
| `thirdparty/ps5-plume-triangle` | Submodule: PS5 payload SDK, RADV, app CRT and FSELF signer |
| `patches/` | FFmpeg configuration for the PS5 |
| `tools/` | Uploading to the PS5, GPU frame report reader, resolve picture decoder |
| `disc-files/` | Where your own discs go (never committed) |

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay, built on the
  [Xenia](https://github.com/xenia-project/xenia) emulator's research and code.
- gta4-ps5 by elripalda: the PS5 runtime, link and signing recipe this port's are based on.
- [ps5-plume-triangle](https://github.com/Nazky/ps5-plume-triangle) and
  [plume-ps5](https://github.com/Nazky/plume-ps5) by Nazky.
- [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) (RADV) and
  [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) by mihawk-99.
- [Roboto](https://fonts.google.com/specimen/Roboto) font, Apache License 2.0.

## License

This port's code is under the [GNU General Public License v3.0](LICENSE) (the packaging script
is derived from gta4-ps5's GPL-3.0 one). The ReXGlue SDK in `thirdparty/rexglue-sdk` keeps its own
license ([LICENSE](thirdparty/rexglue-sdk/LICENSE)), and the submodules keep theirs. The game
itself is not covered by any of this and is not part of this project.
