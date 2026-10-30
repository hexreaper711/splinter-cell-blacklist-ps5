# Your disc files

Splinter Cell: Blacklist comes on two Xbox 360 discs. You need **your own** copy of both.

## For the build: disc 1's `default.xex`

The build translates the game's code from disc 1's executable. Put it here:

```
disc-files/disc1/default.xex
```

Disc 2's `default.xex` holds the same game code (the same 130,242 functions, every one
identical), so the build doesn't need it. Only the PS5 does.

## For the PS5: both discs, whole

After installing the app, copy the full root of each extracted disc (everything except
`$SystemUpdate`) into the app on the PS5:

```
/data/homebrew/PPSA90827/game/disc1/   <- Disc 1: Campaign Part 1 & Multiplayer
/data/homebrew/PPSA90827/game/disc2/   <- Disc 2: Campaign Part 2 & HD Texture Pack
```

- **Disc 1** alone is enough to start the game.
- **Disc 2** is needed for the second half of the campaign. It also holds the HD Texture Pack
  (`Content/` on the disc, about 3 GB), which the launcher installs once when you start a disc.
  The launcher shows which discs it found.

Nothing in this folder is committed (`.gitignore`), and this repository never contains any game
files.
