# How EasyCast was made

This page tells what was changed in Flycast for the Anbernic RG DS Plus, why, what each step gave, and what was tried
and dropped. Every step was measured on the handheld itself with the same savestates (Dead or Alive 2 heavy fight,
Crazy Taxi, Sonic Adventure 2, Jet Set Radio, GTA 2, Soul Calibur, Shenmue), and the picture was checked by eye after
every change. Rule from the start: never trade picture, sound or accuracy for speed.

## The hardware

Rockchip RK3568: four Cortex-A55 cores at up to 1.99 GHz sharing one clock, Mali G52 GPU, about 1 GB of RAM, two
640x480 screens. The chip slows itself down at 70 °C. A Dreamcast game keeps one core fully busy with SH4 emulation,
so the speed of that one thread decides almost everything.

## Steps that worked

| Step | Why | Result |
|---|---|---|
| Standalone Flycast 2.7 instead of the libretro core | fewer layers, newer code | Dead or Alive 2 from about 25 to about 30 FPS |
| Vulkan on the Arm Mali driver (instead of OpenGL ES) | far less CPU time per draw call | about 30 to about 47 FPS |
| Asynchronous page flip on the chosen screen | the render thread no longer waits for the GPU | about 47 to about 52 FPS |
| Sound (AICA) in its own thread | the SH4 thread no longer runs the sound chip | about 52 to 59 FPS |
| Profile guided build (PGO) | the compiler optimizes for the real hot paths | a few percent |
| Fast-forward of the games' wait loops | games poll a counter while waiting for the next frame; the emulator now skips that time | SH4 thread 20 points less busy, Crazy Taxi no longer reaches 70 °C |
| The emulator picks the GPU clock | the Mali kernel driver reports no load, so the system kept the GPU at full clock | GPU at 200 to 600 MHz in light games, cooler |
| Shared texture descriptors, savestates in the background | less work and no pauses in the render thread | render thread 11 to 13% less busy |
| Disc read-ahead, prebuilt GPU pipelines | the SD card and first-time shader builds caused pauses | fewer stutters when a level loads |
| Per-pixel transparency: opaque geometry drawn once | the slowest path in Shenmue | GPU 12 to 14% less busy in Shenmue |
| No repeated driver state between polygons, persistent buffer mapping | fewer driver calls | a few percent in the render thread |
| GPU powers down between frames | GPU is busy only half of each frame | 1 to 5% less power, same FPS |

## Tried and dropped

| Idea | Why it was dropped |
|---|---|
| Open source Mali driver build (malisx2) | Shenmue ran slower than with the Arm driver |
| Batching transparent layers | textures flickered |
| Pinning threads to fixed cores | no gain, the system scheduler already does well |
| Waking the sound thread less often | about 0.2%, not worth it |
| Cached (CPU side) GPU buffers | slower on this Mali |
| A CPU clock limit set by the emulator | the system already lowers the clock when there is spare time; a limit only cost frames |
| Lower resolution, frame skipping, per-strip sorting | would change the picture |

## Ideas you can reuse in other emulators

None of these need Dreamcast knowledge. Each one names the files in this repository where it lives.

- **Skip the game's own waiting.** Many games spin in a short loop that reads a counter until the next frame or
  interrupt. The translated code for such a loop can be recognized (one small block that only reads memory, compares
  and jumps back to itself) and the emulator can jump straight to the next scheduled event. Here it freed about 20%
  of the main thread and stopped the overheating in Crazy Taxi. Files: `core/hw/sh4/dyna/easycast_idle.h`.
- **Choose the GPU clock yourself.** Some Mali kernel drivers report no GPU load, so the system keeps the GPU at full
  clock. The emulator already knows how long the GPU worked on each frame (timestamp queries); it can lower the clock
  while there is a big margin and jump back to the top at once when a frame gets close to the budget.
  Files: `core/rend/vulkan/easycast_gpufreq.h`, `core/rend/vulkan/gpu_timer.h`.
- **Never block the render thread on the page flip.** Present through DRM/KMS from a small thread of its own, so the
  thread that builds the next frame does not wait for vertical sync. Files: `core/rend/vulkan/async_present.h`,
  `core/rend/vulkan/drm_presenter.h`.
- **Run the sound chip in its own thread** with a small buffer and a lag limit, so the CPU thread of the console does
  not pay for audio. Files: `core/hw/aica/aica_thread.cpp`.
- **Let the GPU sleep between frames** (Mali `power_policy` = `coarse_demand`) and **profile guided builds**: both are
  free wins on these chips.
- **Measure power from the battery gauge.** `current_avg` and `voltage_avg` of the battery give watts; the gauge
  updates about every 8 s, so compare runs of 60 s, twice in a row.

## What limits it now

Dead or Alive 2 in a heavy fight needs the SH4 thread at the full 1.99 GHz. When the chip reaches 70 °C the system
lowers the clock and the game drops to 54 to 58 FPS. Shenmue's notebook and some cutscenes with lots of transparency
are limited by the GPU. The next big step would be a deeper rework of the SH4 code translator.

## How the numbers were taken

A test bench loads a savestate, waits 5 to 10 s, then records frames per 5 s, time per thread, GPU time per frame,
clocks, temperatures and battery power for 15 to 60 s. Each change was run against the previous version with the same
savestate, usually twice in a row (A, B, A, B), because the handheld warms up during a test.

If you port any of this to another emulator or ship EasyCast in your OS, please tell us in Issues, we will be glad to
hear it and to help.

This work was done with the help of AI (Claude by Anthropic): code changes, measurements and analysis, with every
result checked on the real handheld by the project's author.
