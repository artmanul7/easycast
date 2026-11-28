<p align="center"><img src="media/easycast-logo.png" width="180" alt="EasyCast logo"></p>

<h1 align="center">EasyCast</h1>

<p align="center"><b>Dreamcast on low-power handhelds: Flycast + Vulkan, tuned for the Anbernic RG DS Plus on BananaOS.</b></p>

EasyCast is a build of the [Flycast](https://github.com/flyinghead/flycast) emulator (v2.7) made to run Dreamcast games
smoothly on a small Rockchip RK3566/RK3568 handheld with a Mali G52 GPU, without losing picture quality or accuracy.
It ships its own Vulkan driver stack (Mali userspace driver + kernel module) for the time a game runs and gives the
system's Panfrost driver back when you quit.

## Please take it

The goal of EasyCast is to share as much as possible. If you build a custom OS for low-power handhelds
(ROCKNIX, Knulli, muOS, ArkOS, BananaOS or your own), you are welcome to ship EasyCast as a ready Dreamcast emulator.
If you work on another emulator, most of the ideas here are not Dreamcast specific: skipping a game's wait loops,
choosing the GPU clock from the measured frame time, a page flip that never blocks the render thread, sound in its own
thread. They can make other emulators faster and cooler on the same weak hardware.

Everything is open: the code of every change, the measurements and what did not work. Start with
**[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)**. Questions and ideas are welcome in Issues.

| Document | What is inside |
|---|---|
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | every optimization, why, what it gave, what was dropped, ideas for other emulators |
| [docs/TECHNICAL.md](docs/TECHNICAL.md) | Mali driver switching on BananaOS, two screens, all switches, logs, files, troubleshooting |
| [docs/BUILDING.md](docs/BUILDING.md) | cross-compiling, PGO, building the GPU kernel module for another kernel |
| [CHANGELOG.md](CHANGELOG.md) | what each version changed |

## Results

Measured on the RG DS Plus (BananaOS 0.5.3.4), Vulkan, native 640x480 unless noted:

| Game | Before | EasyCast 0.2.8 |
|---|---|---|
| Dead or Alive 2, heavy fight | about 25 FPS (stock libretro Flycast on BananaOS), about 30 FPS (first EasyCast builds) | 58 to 60 FPS on a cool handheld |
| Soul Calibur | 10 to 15 FPS felt (stock firmware Flycast) | 60 FPS, even at 1.5x resolution |
| Crazy Taxi | 60 FPS, but the handheld reached 70 °C and slowed down after 5 minutes | 60 FPS, stays under 68 °C, no slowdown |
| Sonic Adventure 2 | | 60 FPS |
| Shenmue (per-pixel transparency) | | its native 30 FPS in most scenes |

Picture, sound and accuracy are the same as stock Flycast: no frame skipping, no lower resolution, no hacks per game.
Long sessions in Dead or Alive 2 still drop to 54 to 58 FPS when the chip gets hot (the system lowers the CPU clock
at 70 °C). How it was done, and what did not work: **[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)**.

## What is different from stock Flycast

- **Faster SH4 emulation on ARM64**: native FTRV/FIPR, fewer memory round trips in translated code, fast-forward of
  the games' own wait loops (SDK frame wait, RAM polling). Sound (AICA) runs in its own thread.
- **Vulkan tuned for Mali**: asynchronous page flip on the chosen screen of a two-screen handheld, persistent buffer
  mapping, no repeated pipeline/descriptor binds, prebuilt pipelines, cheaper per-pixel transparency.
- **Less heat**: the emulator sets the GPU clock from the measured GPU time per frame, and the GPU powers down between
  frames.
- **Handheld controls**: swap D-Pad and analog stick (Settings → Controls), brightness of the game screen
  (Settings → Video, or **Select + Up / Down** in game).
- **Updates inside the emulator**: Settings → About → *Check for updates*.

## New settings

| Setting | Where | What it does |
|---|---|---|
| **GD-ROM Fast Loading** | Settings → Advanced → Other | The disc drive works much faster than on a real Dreamcast: shorter loading screens and fewer pauses when a level streams data. **Use with care.** It is not accurate, so it is off by default. I ran into a real problem: with it on, the videos in Grandia II did not start and the emulator quit with an error (133). Other games may break in other ways, for example music cut on a loading screen. Turn it on only for a game that works with it, using per-game settings. |
| **Swap D-Pad and analog stick** | Settings → Controls | The stick works as the D-Pad and the D-Pad as the stick. Handy for games that want the stick where the D-Pad is more comfortable, or the other way round. Can be set per game. |
| **Screen brightness** | Settings → Video, or Select + Up / Down in game | Brightness of the screen the game is on. The menu of the handheld keeps its own brightness. |
| **Screen (top / bottom)** | Settings → Video | Which screen of a two-screen handheld shows the game; the other one is switched off to save power. |
| **Check for updates** | Settings → About | Finds and installs a new EasyCast version from GitHub. |

## Compatibility

EasyCast needs a handheld with the **Rockchip RK3566 or RK3568** chip (four Cortex-A55 cores, Mali-G52 GPU), 64-bit
Linux, and a GPU kernel module built for the kernel of the system.

| Status | What |
|---|---|
| ✅ Tested | Anbernic RG DS Plus (RK3568, two screens) on BananaOS 0.5.2.2 and 0.5.3.4 (kernel 7.2.7-banana) |
| 🧪 Community test | Anbernic RG DS on BananaOS 0.5.3.6 (kernel 7.2.7-banana). Vulkan worked on the top screen with a patched 0.2.8 binary. The 0.2.9 package is available for testing. |
| ⚠️ Same chip, untested | the other handhelds in the list below, on BananaOS or any system once the GPU module fits its kernel |
| ⚠️ One screen | untested, EasyCast was only tried on two screens |
| ❌ Different chip | handhelds on RK3326, RK3399, RK3588, Allwinner (H700, A133) or Snapdragon/MediaTek (for example RG351, RG552, RG CubeXX, RG35XX, Trimui Brick, Retroid Pocket): another GPU, the bundled driver does not fit |

Thanks to [@RockwellShah](https://github.com/RockwellShah) for fixing Vulkan support for the RG DS's 640x480
panels and testing the fix on his handheld. See [PR #3](https://github.com/artmanul7/easycast/pull/3) and the
[EasyCast 0.2.9 prerelease](https://github.com/artmanul7/easycast/releases/tag/v0.2.9).

### Handhelds with the same chip (might work)

All of these use the RK3566 (or the RK3568, its close relative) with a Mali-G52 GPU, like the RG DS Plus. The list is
from public specifications and the ROCKNIX and Batocera device lists. **It is not complete**: there are more
RK3566/RK3568 handhelds from smaller makers, and new ones keep appearing. Test results for the two RG DS models are
listed above.

| Maker | Models | Chip | Screens |
|---|---|---|---|
| Anbernic | **RG DS Plus** (tested) | RK3568 | two, 1024x768 |
| Anbernic | RG DS (community test) | RK3568 | two, 640x480 |
| Anbernic | RG353P, RG353M, RG353V, RG353VS, RG353PS | RK3566 | one |
| Anbernic | RG503 | RK3566 | one, OLED |
| Anbernic | RG ARC-D, RG ARC-S | RK3566 | one |
| Powkiddy | RGB30, RK2023, RGB10 Max 3, RGB20SX, RGB20 Pro, X55, X35S, X35H | RK3566 | one |
| Miyoo | Flip, Flip V2 (clamshell) | RK3566 | one |
| MiniLoong | Pocket One | RK3566 | one |

What it takes for another device:
- the **GPU kernel module** (`mali_kbase.ko`) built for that system's kernel, see [docs/BUILDING.md](docs/BUILDING.md);
- the launcher and screen code were written for the RG DS Plus (two panels, `backlight0` and `backlight1`), so
  another device may need small changes there;
- about 1 GB of RAM is enough for normal games; per-pixel mode (Shenmue) wants a lot more free memory.

The installer and the launcher check one hard rule: the module must be built for exactly the running kernel
(`uname -r`). The OS name and device are only reported.

**Ports are welcome.** If you build the module for your kernel, or want EasyCast in your OS image, go ahead: all the
sources are open (see below). I would be glad to hear about it. If you try it on one of these handhelds, please write
the result in Issues so the list can be updated.

## Requirements

- BananaOS with kernel 7.2.7-banana on an RK3566/RK3568 handheld (see compatibility above), Wi-Fi for installing and updates
- About 100 MB free on the card
- Dreamcast games as `.chd`, `.gdi`, `.cdi` or `.cue` in `/data/roms/dreamcast`
- An original BIOS is optional: without it Flycast's built-in HLE BIOS is used

## Known issues

- Dead or Alive 2 drops to 54 to 58 FPS after several minutes of heavy fighting, when the chip reaches 70 °C.
- Shenmue's notebook and some cutscenes with lots of transparency are slower than 30 FPS (GPU limit).
- The first time a scene appears there can be a short stutter while GPU pipelines are built; then they are cached.
- GD-ROM Fast Loading can break games. In Grandia II the videos did not start and the emulator quit with error 133. Keep it off, or enable it for one game only.
- One-screen devices are untested.

## Install

1. Connect the handheld to Wi-Fi.
2. Download **[Install-EasyCast.sh](https://github.com/artmanul7/easycast/releases/latest/download/Install-EasyCast.sh)**
   and put it into the **Apps** folder: `/data/store/tiles` (on the BananaOS network share: `store\tiles`).
3. On the handheld open **Apps** and start **Install-EasyCast**. It downloads the latest release, checks its SHA-256
   and the kernel, installs to `/data/banana/easycast`, adds the **EasyCast** tile and removes itself.
4. Put your games (`.chd .gdi .cdi .cue`) into `/data/roms/dreamcast`. An original BIOS is optional
   (`/data/bios/dc`); without it Flycast's built-in HLE BIOS is used.

## Update

Settings → About → **Check for updates** → **Install x.y.z**, then quit and start EasyCast again.
EasyCast also checks once a day at start (when online) and shows a short notice if a new version exists.
Your settings and saves (`/data/banana/easycast/userdata`) are never touched by updates; the previous version is kept
until the next update.

## Remove

Delete the folder `/data/banana/easycast` and the tile `/data/store/tiles/EasyCast.sh`.

## Controls (RG DS Plus)

| Buttons | Action |
|---|---|
| Menu | Settings |
| Select + Menu | Quit |
| Select + Up / Down | Screen brightness |

## Sources and licences

- Flycast with all EasyCast changes: **[this repository, branch `easycast`](https://github.com/artmanul7/easycast/tree/easycast)** (GPL-2.0).
- SDL2 (patched), `mali_kbase` module (GPL-2.0), libmali hook and kernel build inputs: `easycast-x.y.z-gpl-sources.tar.gz` in every release.
- Mali userspace driver: binary from Arm/Rockchip (via [ROCKNIX/libmali](https://github.com/ROCKNIX/libmali)), unchanged, under the Mali EULA included in the package.
- Flycast is made by flyinghead and contributors. BananaOS by [JorreFog](https://github.com/JorreFog/BananaLinux).

EasyCast is a fan project, not affiliated with SEGA. Dreamcast is a trademark of SEGA. Bring your own games.

## About

AI was used during development. Bug reports and pull requests are welcome.

**Keywords:** Dreamcast emulator, Flycast fork, Vulkan, Mali G52, Bifrost, libmali, mali_kbase, Panfrost, RK3566,
RK3568, Rockchip, Anbernic RG DS Plus, RG DS, RG353, RG503, RG ARC, Powkiddy RGB30, Miyoo Flip, dual screen handheld, ARM64, Cortex-A55, SH4 dynarec, BananaOS, low power,
thermal throttling, GPU DVFS, retro handheld.

---

### По-русски

EasyCast это эмулятор Dreamcast (Flycast + Vulkan) для BananaOS на Anbernic RG DS Plus. **Установка:** подключите Wi-Fi,
скачайте `Install-EasyCast.sh`, положите в папку Apps (`/data/store/tiles`) и запустите его из Apps. Игры кладите в
`/data/roms/dreamcast`. **Обновление:** Settings → About → Check for updates. **Удаление:** удалите папку
`/data/banana/easycast` и ярлык `EasyCast.sh`. Новые настройки: ускоренный привод GD-ROM (Settings → Advanced → Other), смена местами крестовины и стика (Settings → Controls), яркость экрана (Settings → Video или Select + вверх/вниз), выбор экрана, проверка обновлений (Settings → About).

Буду рад, если разработчики своих прошивок для слабых приставок возьмут EasyCast себе как готовый эмулятор, а авторы других эмуляторов используют эти наработки. Всё открыто: код, замеры и то, что не сработало (docs/DEVELOPMENT.md).

При разработке был использован ИИ.
