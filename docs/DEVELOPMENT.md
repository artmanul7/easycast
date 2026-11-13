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

## What limits it now

Dead or Alive 2 in a heavy fight needs the SH4 thread at the full 1.99 GHz. When the chip reaches 70 °C the system
lowers the clock and the game drops to 54 to 58 FPS. Shenmue's notebook and some cutscenes with lots of transparency
are limited by the GPU. The next big step would be a deeper rework of the SH4 code translator.

## How the numbers were taken

A test bench loads a savestate, waits 5 to 10 s, then records frames per 5 s, time per thread, GPU time per frame,
clocks, temperatures and battery power for 15 to 60 s. Each change was run against the previous version with the same
savestate, usually twice in a row (A, B, A, B), because the handheld warms up during a test.

This work was done with the help of AI (Claude by Anthropic): code changes, measurements and analysis, with every
result checked on the real handheld by the project's author.
