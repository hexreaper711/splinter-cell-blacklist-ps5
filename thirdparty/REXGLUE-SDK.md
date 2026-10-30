# ReXGlue SDK in this repository

Copied from rexglue/rexglue-sdk (https://github.com/rexglue/rexglue-sdk) v0.10.0, commit
f5337cd, with the PS5 port's changes on top (branch ps5-port, commits below, newest first).
Its own third-party libraries are submodules of this repository (see /.gitmodules).
tools/binutils (Windows-only PowerPC test tools, ~120 MB) is left out.

```
d5b5a8b 2026-10-30 [ps5 port] GPU: depth pyramid mip size, pixel position at 2x, readback modes
f441f6b 2026-10-30 [ps5 port] Disc swap, leaving the game, pad-driven dialogs, faster STFS reads
96aa7c0 2026-10-30 codegen: scaled jump table offsets, virtual-call stubs nothing points to
3dc05d0 2026-10-23 codegen: callbacks stored to memory too; any b before one
0a96f66 2026-10-23 codegen: a backward b also ends a routine before a data pointer
17743d0 2026-10-23 codegen: also register callbacks handed to a call
ce77aa2 2026-10-23 [ps5 port] Recompiled DLL modules linked into the program
65c5b6e 2026-10-23 codegen: register functions found only through pointers in data
0bf4cbc 2026-10-23 [ps5 port] XMA context states in the PS5 snapshot
781305b 2026-10-23 Vulkan pipeline storage: flush before waiting; PS5 timing stats
69d9147 2026-10-23 [ps5 port] Diagnostics for the story hang and the slow fights
70e0e26 2026-10-23 PS5: take out the fork's non-PS5 changes that came with its PS5 hunks
96aaca3 2026-10-23 [ps5 port] PS5 audio driver: pass the stereo fold and output gain to 0.10.0's downmix
d15b5cd 2026-10-23 PS5: device.h and xam_input.cpp without fork-only code; disc image reader back to 0.10.0
e64b7b1 2026-10-23 PS5: guest memory in direct memory, AllocFixed/SHM_ANON, file copies, TSC clock, /app0 paths, fibers, DualSense on the device API, window/surface, critical sections, codegen pch (lock depth, fences, phys offset), renderdoc/sha256
7d35864 2026-10-23 PS5: UI files back to 0.10.0 with only their PS5 parts (VK_KHR_display surface and instance, memory recycler, PS5 main); global lock spin mutex; float parsing; fork-only code removed
e338e19 2026-10-23 PS5: exception handler (FreeBSD signal context, SIGBUS) and threading (signals, ids, direct-memory stacks) merged by hand
04cf1f7 2026-10-23 PS5: gta4-ps5's PS5 hunks that apply cleanly to 0.10.0 (tools/ps5_hunks.py)
f1a03c0 2026-10-23 PS5: SDL on dummy video/audio only, default code model in rexglue_apply_target_settings
80311b4 2026-10-22 PS5: platform detection, static runtime and Xenos plugin, PS5 audio/input/surface files from gta4-ps5
c1b93d0 2026-10-22 Broken Bond's XMA drained-EOS patch (patches/sdk/rexglue-sdk-v0.10.0.patch)
f5337cd 2026-08-20 cd: build linux-amd64 in a jammy container to hold the glibc floor at 2.35
```
