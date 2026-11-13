# Technical notes

For people who want to port EasyCast, ship it in an OS image, or reuse parts of it.

## How the Mali Vulkan driver is used on BananaOS

BananaOS runs the open source Panfrost driver (Mesa) for everything. Panfrost on the Mali G52 (Bifrost) is fine for
2D and older systems, but Flycast needs a fast Vulkan driver. EasyCast therefore switches drivers only for the time a
game runs (`runtime/<version>/run.py`):

1. `rmmod panfrost`
2. `insmod mali_kbase.ko` (the Arm kernel driver from [ROCKNIX/mali_kbase](https://github.com/ROCKNIX/mali_kbase),
   commit 39da994, built for the BananaOS kernel 7.2.7-banana; source in the release assets)
3. GPU devfreq governor `performance` (the emulator then sets `max_freq` itself), `power_policy` = `coarse_demand`
4. start Flycast with the Arm userspace driver `libmali` g29p1 (Vulkan 1.1) from `runtime/<version>/lib`
   (`VK_ICD_FILENAMES` points to the bundled `mali.json`, `LD_LIBRARY_PATH` to `lib`)
5. after Flycast exits: GPU `max_freq` back to the top, `rmmod mali_kbase`, `modprobe panfrost`

Nothing in the OS is replaced on disk. The module must be built for exactly the running kernel; the launcher compares
the module's `vermagic` with `uname -r` (`compat.py`).

## Two screens

The Arm driver's `VK_KHR_display` only sees the first DRM connector (the bottom panel on the RG DS Plus). EasyCast
renders to an offscreen image and shows it with its own small DRM/KMS presenter on the panel chosen in
Settings → Video (`core/rend/vulkan/drm_presenter.h`, `async_present.h`): the images are shared with DRM as dma-bufs
(no CPU pixel copies) and the page flip runs in a separate thread, so the render thread never waits for it. The unused panel's backlight is switched off while a game runs.
On a one-screen device this path is untested.

## Environment switches

Every EasyCast change can be switched off for comparison, without rebuilding. Set them in the tile script before
`exec python3 ...run.py`. Value `0` switches a feature off unless noted.

| Variable | Default | What |
|---|---|---|
| `EASYCAST_ASYNC_PRESENT` | 1 (set by the tile) | page flip from its own thread |
| `EASYCAST_AICA_THREAD` | on | sound chip in its own thread |
| `EASYCAST_AICA_WAKE` | 24 | sound thread wake batch (samples) |
| `EASYCAST_IDLE` | on | fast-forward of the games' wait loops |
| `EASYCAST_JCOND_REG` | on | jump condition kept in a register in translated code |
| `EASYCAST_GPU_DVFS` | on | the emulator chooses the GPU clock |
| `EASYCAST_GPU_POLICY` | coarse_demand | Mali `power_policy` (launcher); `always_on` = GPU never sleeps |
| `EASYCAST_PERSIST_MAP` | on | Vulkan buffers stay mapped |
| `EASYCAST_MAIN_CACHED` | off | `1` = per-frame buffers in cached memory (slower on this Mali) |
| `EASYCAST_SHARE_DESC` | on | shared texture descriptors |
| `EASYCAST_PIPE_CACHE` | on | prebuilt pipelines per game (`data/flycast/easycast-pipes-*`) |
| `EASYCAST_DISC_PREFETCH` | on | disc read-ahead |
| `EASYCAST_ASYNC_SAVE` | on | savestates written in the background |
| `EASYCAST_PIN` | `Flycast-emu=3;Flycast-rend=0-2;Flycast-aica=1` | thread to core pinning |
| `EASYCAST_OIT_Z1` | on | per-pixel mode: opaque geometry drawn once |
| `EASYCAST_HITCH=<file>` | off | stutter catcher: every frame longer than `EASYCAST_HITCH_MS` with its cause |
| `EASYCAST_TIMELINE=<file>` | off | per-frame timeline of the emulation and render threads |
| `EASYCAST_BLOCKDUMP=<file>` | off | map of translated SH4 blocks (for profiling) |

The run log (`userdata/logs/run-*.log`) gets one line per 5 s for most features: FPS (`EASYCAST_SAMPLE`), GPU time
and clock (`EASYCAST_GPU`, `EASYCAST_GPUFREQ`), skipped wait loops (`EASYCAST_IDLE`), sound thread
(`EASYCAST_AICA`), CPU clock and temperatures (`EASYCAST_HARDWARE`).

## Files

| Path | What |
|---|---|
| `/data/banana/easycast/current` | version the EasyCast tile starts |
| `/data/banana/easycast/runtime/<version>/` | emulator, drivers, scripts |
| `/data/banana/easycast/userdata/config/flycast/emu.cfg` | settings (per-game sections like `[CD1]`) |
| `/data/banana/easycast/userdata/data/flycast/` | savestates, VMU saves, pipeline caches |
| `/data/banana/easycast/userdata/logs/` | run logs |
| `/data/roms/dreamcast/` | games |
| `/data/bios/dc/` | optional original BIOS (`dc_boot.bin`, `dc_flash.bin`) |

## Troubleshooting

- **"Select Vulkan rendering" on start.** After a Vulkan failure Flycast saves OpenGL as the renderer. Set
  `pvr.rend = 4` (or `5` for per-pixel) in `emu.cfg`.
- **Black screen or the GPU module does not load.** Read the newest `userdata/logs/run-*.log`. Usually the kernel
  differs from the one the module was built for.
- **Error 133 ("Verify Failed ... bm_AddBlock" in the log) while a game loads.** Seen in Grandia II with
  GD-ROM Fast Loading on. Switch it off (Settings → Advanced → Other), or use it only per game.
- **Short stutters the first time a scene appears.** The GPU pipelines of that game are built once and then cached.
- **Slower after a few minutes in heavy games.** The chip reached 70 °C and the system lowered the CPU clock.
