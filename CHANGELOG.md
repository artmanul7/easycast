# Changelog

Dead or Alive 2 heavy fight is the main speed test (frames per second on the RG DS Plus, native 640x480).

## 0.2.9
- The Vulkan DRM presenter accepts the 640x480 panel mode on the Anbernic RG DS alongside the 1024x768 mode on the RG DS Plus.
- Thanks to [@RockwellShah](https://github.com/RockwellShah) for the fix and for testing Vulkan on the RG DS with a patched 0.2.8 binary ([PR #3](https://github.com/artmanul7/easycast/pull/3)).

## 0.2.8 (first public release)
- GPU powers down between frames (Mali `power_policy` `coarse_demand`): 1 to 5% less power, same FPS
- Screen brightness of the game panel: Settings → Video and Select + Up / Down
- Only the "Swap D-Pad and analog stick" option stays (the "D-Pad also moves the stick" option was removed)
- Installer from GitHub, updates in Settings → About, compatibility check by kernel version

## 0.2.7
- Translated SH4 code keeps the conditional jump flag in a register (no measurable change, kept as cleaner code)

## 0.2.6
- Vulkan buffers stay mapped: 2 to 3% less time uploading vertices

## 0.2.5
- No repeated pipeline, texture set or constants sent to the driver between polygons (Crazy Taxi faster)

## 0.2.4
- Controls: swap D-Pad and analog stick
- Per-pixel transparency: opaque geometry drawn once without shadow volumes (GPU 12 to 14% less busy in Shenmue)
- Cheaper checks of the wait-loop fast-forward

## 0.2.3
- Disc read-ahead, prebuilt GPU pipelines per game (fewer stutters on loading), "GD-ROM Fast Loading" checkbox

## 0.2.2
- Savestates written in the background, shared texture descriptors (render thread 11 to 13% less busy), stutter log

## 0.2.1
- Two more kinds of game wait loops fast-forwarded: SH4 thread 20 points less busy, Crazy Taxi no longer overheats

## 0.2.0
- The emulator chooses the GPU clock from the measured GPU time (200 to 800 MHz)

## 0.1.9
- Fast-forward of the Dreamcast SDK frame wait loop (Dead or Alive 2, GTA 2)

## 0.1.8
- Profile guided build (PGO)

## 0.1.7
- Cheaper sound thread, threads pinned to cores

## 0.1.6
- Sound chip (AICA) in its own thread: Dead or Alive 2 about 52 → 59 FPS

## 0.1.5 and earlier
- Standalone Flycast 2.7 with the Arm Mali Vulkan driver, asynchronous page flip on the top screen:
  Dead or Alive 2 about 30 (OpenGL ES) → 47 (Vulkan) → 52 FPS (asynchronous flip)
