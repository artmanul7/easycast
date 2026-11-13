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

## Compatibility

| Device | OS | Status |
|---|---|---|
| Anbernic RG DS Plus (RK3568, Mali G52, two screens) | BananaOS 0.5.2.2 / 0.5.3.4 (kernel 7.2.7-banana) | ✅ tested |
| Other RK3566/RK3568 devices | BananaOS with the same kernel | ⚠️ should start, untested |
| One-screen devices | any | ⚠️ untested (EasyCast was only tested on two screens) |
| Other systems (ROCKNIX, Knulli, muOS, ArkOS…) | their own kernels | ❌ the GPU module must match the kernel |

The installer and the launcher check one hard rule: the bundled GPU module (`mali_kbase.ko`) must be built for exactly
the running kernel (`uname -r`). The OS name and device are only reported.

**Ports are welcome.** If you build the module for your kernel, or want EasyCast in your OS image, go ahead: all the
sources are open (see below). We would be glad to hear about it.

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

## Made with AI

EasyCast was built with the help of AI. The code changes, the measurements on the handheld, the packaging and this
page were made together with Claude (an AI assistant by Anthropic) and checked by hand on a real RG DS Plus.
Bug reports and pull requests are welcome.

---

### По-русски

EasyCast это эмулятор Dreamcast (Flycast + Vulkan) для BananaOS на Anbernic RG DS Plus. **Установка:** подключите Wi-Fi,
скачайте `Install-EasyCast.sh`, положите в папку Apps (`/data/store/tiles`) и запустите его из Apps. Игры кладите в
`/data/roms/dreamcast`. **Обновление:** Settings → About → Check for updates. **Удаление:** удалите папку
`/data/banana/easycast` и ярлык `EasyCast.sh`. Яркость экрана: Select + вверх/вниз.

Буду рад, если разработчики своих прошивок для слабых приставок возьмут EasyCast себе как готовый эмулятор, а авторы других эмуляторов используют эти наработки. Всё открыто: код, замеры и то, что не сработало (docs/DEVELOPMENT.md).

Проект сделан с помощью ИИ (Claude от Anthropic): правки кода, замеры на приставке, сборка и эта страница. Всё проверено вручную на настоящей RG DS Plus.
