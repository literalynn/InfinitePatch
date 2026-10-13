<p align="center">
  <img src="assets/logo.svg" width="600" alt="InfinitePatch" />
</p>

<p align="center">
A patch that makes the PC version of BioShock Infinite run properly on modern hardware and Windows 11.
</p>

<p align="center">
  <a href="https://github.com/literalynn/InfinitePatch/releases/latest"><img src="https://img.shields.io/github/v/release/literalynn/InfinitePatch?style=flat-square&color=c9973f" alt="Latest release" /></a>
  <img src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-16324a?style=flat-square" alt="Windows 10 | 11" />
  <a href="LICENSE"><img src="https://img.shields.io/github/license/literalynn/InfinitePatch?style=flat-square&color=16324a" alt="MIT license" /></a>
</p>

Borderless fullscreen without alt-tab lag, no more crash after the intro videos, no more framerate drops with high polling rate mice, constant mouse sensitivity while sprinting, intro videos skipped, frame cap, VRR, UI scaling, wider FOV and more. Every option can be turned on or off in a simple `.ini` file.

InfinitePatch does **not** modify any game file. It edits the game's settings files and the Windows per-game graphics setting, and adds one small DLL next to the game (`dinput8.dll`) for the fixes that settings can't do. Everything can be turned off and fully uninstalled.

## How to Install

> [!NOTE]
> Made and tested with the Steam version. The GOG and Epic Games Store versions use the same game files and should work too: please open an issue if they don't.
>
> 1. Launch the game once, then close it. It creates its settings files on first launch.
> 2. **Download**: [InfinitePatch.zip](https://github.com/literalynn/InfinitePatch/releases/latest/download/InfinitePatch.zip)
> 3. Extract the contents of the zip into the game's **main folder**, the one that contains the `Binaries` and `XGame` folders. This also puts `dinput8.dll` in `Binaries\Win32`.
> 4. Double-click **`Install.bat`**.
>
> Directory path for Steam:
> `SteamLibrary\steamapps\common\BioShock Infinite`

> [!IMPORTANT]
> Run `Install.bat` again every time you edit `InfinitePatch.ini`, and after the game is updated or verified by Steam: the game rebuilds its settings files when its own files change.

> [!WARNING]
> Other mods that also install a `dinput8.dll` in `Binaries\Win32` can't be used at the same time as InfinitePatch's native fixes. `Install.bat` tells you when that's the case.

> [!TIP]
> If Windows shows a security warning when you run `Install.bat`, click **Run** (or **More info** → **Run anyway**). To avoid it, right-click the zip → **Properties** → tick **Unblock** before extracting.

## How to Uninstall

Double-click **`Uninstall.bat`**. It removes `dinput8.dll` and restores the settings files and the Windows graphics setting saved during the first install. You can then delete the remaining InfinitePatch files from the game folder.

Options you changed in the game's menus after installing the patch are reverted too.

## Configuration

Every option lives in `InfinitePatch.ini`, with a description and the accepted values above each line. Edit it with Notepad, save, then run `Install.bat` again.

| Section | Option | Default | Values |
| --- | --- | --- | --- |
| Display | `DisplayMode` | `2` | `0` windowed, `1` exclusive fullscreen (vanilla), `2` borderless fullscreen |
| Display | `ResolutionX` / `ResolutionY` | `0` | `0` = native resolution of your main monitor, or a size in pixels |
| Display | `VSync` | `1` | `0` off, `1` on (caps the game at your refresh rate) |
| Display | `MaxFPS` | `0` | `0` no limit, or a framerate limit |
| Display | `UIScale` | `1.0` | `0.0` vanilla to `1.0` full scaling with resolution |
| Display | `FOVSliderRange` | `50` | `15` vanilla, up to `100` |
| Fixes | `FixHighPollingRate` | `1` | `0` off, `1` on |
| Fixes | `FixSprintSensitivity` | `1` | `0` off, `1` on |
| Fixes | `ForceDedicatedGPU` | `1` | `0` off, `1` on |
| Fixes | `WindowedOptimizations` | `1` | `0` off, `1` on |
| General | `SkipIntro2K`, `SkipIntroIrrational`, `SkipIntroUnreal`, `SkipIntroAMD` | `1` | `0` play the video, `1` skip it |
| Input | `DisableControllers` | `0` | `0` off, `1` ignore all controllers |
| Graphics | `LensFlares`, `DepthOfField`, `MotionBlur`, `HighQualityLightShafts` | `-1` | `-1` keep the game's setting, `0` off, `1` on |
| Graphics | `ShadowResolution` | `0` | `0` keep the game's setting, `1024`, `2048`, `4096`, `8192` |

Invalid values are rejected with a clear message and nothing is changed.

`FixHighPollingRate` and `FixSprintSensitivity` are read by `dinput8.dll` every time the game starts, so they apply even without running `Install.bat` again.

# Features

## High Polling Rate Mouse Fix

With a 1000 Hz to 8000 Hz gaming mouse, the framerate collapses as soon as you turn the camera quickly: in testing with a 4000 Hz mouse, from 400 FPS down to 70-200 FPS, with frames taking up to 230 ms.

Windows sends the game a mouse move message every time the cursor has moved since the game last checked its messages. A fast, high polling rate mouse moves it between every single message, so the game gets hundreds of mouse moves per frame and handles each one. InfinitePatch lets a single mouse move through per frame, the most recent one, as in the [fix the Godot engine made](https://godotengine.org/article/fixing-high-polling-rate-mice-on-windows/) for the same problem. The camera movement itself is read with Raw Input on a separate thread instead of DirectInput, and handed to the game in a few events per frame.

With the fix, the same 4000 Hz mouse turning the camera fast stays around 400 FPS, with frames under 7 ms.

Disable with `FixHighPollingRate = 0` in `InfinitePatch.ini`.

## Sprint Sensitivity Fix

While you sprint, the game cuts the mouse sensitivity: horizontal turning drops to half speed and vertical to 70%, then goes back to normal when the sprint ends. InfinitePatch compensates mouse movement for exactly that amount while a sprint is on, so the camera turns the same whether you walk or sprint. Aiming and everything else keep the game's own sensitivity.

Disable with `FixSprintSensitivity = 0` in `InfinitePatch.ini`.

> **Note**: This fix uses addresses inside the game's executable and only works with the current Steam version (May 2022 build). On any other version it turns itself off.

## Borderless Fullscreen

The game starts in exclusive fullscreen. On modern setups (high refresh rate monitors, multiple monitors, VRR), alt-tabbing out of it is slow and can lag or freeze the whole PC.

InfinitePatch switches the game to its built-in **Windowed (Fullscreen)** mode, at your monitor's native resolution. Alt-tab is instant and the game behaves like a modern borderless game.

Change with `DisplayMode` in `InfinitePatch.ini`.

## Dedicated GPU Fix

On PCs that also have an integrated GPU (AMD Ryzen 7000/9000 desktop CPUs, most Intel CPUs), Windows can start this old 32-bit DirectX 11 game on the integrated graphics instead of your graphics card. This can cause a crash right after the intro videos and very poor performance.

InfinitePatch sets the game's GPU preference to **High performance**, the same setting as *Settings → System → Display → Graphics* in Windows.

Disable with `ForceDedicatedGPU = 0` in `InfinitePatch.ini`.

## Windowed Game Optimizations

Turns on the Windows 11 **Optimizations for windowed games** for `BioShockInfinite.exe`. The game's old DirectX 11 presentation is upgraded to the modern flip model, which gives borderless mode the same latency as exclusive fullscreen, G-Sync/FreeSync support and instant alt-tab.

Disable with `WindowedOptimizations = 0` in `InfinitePatch.ini`.

## Auto Resolution

Detects the native resolution of your main monitor and applies it, even when Windows display scaling is above 100%.

Set `ResolutionX` and `ResolutionY` in `InfinitePatch.ini` to use a specific resolution instead.

## VSync and Framerate Limiter

Without a cap, the game renders as many frames as it can, even in menus. VSync is enabled by default: combined with borderless mode, the flip model and a VRR monitor, it caps the game at your refresh rate (480 FPS on a 480 Hz monitor) with minimal input latency.

You can also set a framerate limit with `MaxFPS`, which uses the engine's built-in limiter. Set `VSync = 0` and `MaxFPS = 0` for an uncapped framerate.

## Skip Intro Videos

Skips the intro videos on game launch:
- **SkipIntro2K**: 2K Games logo
- **SkipIntroIrrational**: Irrational Games logo
- **SkipIntroUnreal**: Unreal Engine logo
- **SkipIntroAMD**: AMD Gaming Evolved logo

Each one can be enabled individually in `InfinitePatch.ini`.

> **Note**: The legal notice screen is still shown. It is not a video but the screen the game displays while it loads.

## UI Scaling

The game's interface (HUD and menus) doesn't scale with resolution by default, so it gets small at 1440p and 4K. InfinitePatch scales it with your resolution.

Adjust with `UIScale` in `InfinitePatch.ini` (`0.0` = vanilla).

## Wider FOV Slider

The in-game field of view slider only goes from about 60 to 80 degrees. InfinitePatch widens its range to up to 105 degrees by default. Your field of view doesn't change until you move the slider in the options.

The slider maximum is `70 × (1 + FOVSliderRange / 100)`: `15` = 80 (vanilla), `50` = 105, `70` = 119.

## Disable Controllers

The game detects every controller connected to the PC, including virtual ones created by tools like DS4Windows, ViGEm or vJoy. This can make the camera spin on its own and show Xbox button prompts while playing with a keyboard and mouse.

Enable with `DisableControllers = 1` in `InfinitePatch.ini`.

## Graphics Tweaks

Settings that are hidden or limited in the game's menus:
- **LensFlares**: lens flares and lens dirt
- **DepthOfField**: depth of field blur
- **MotionBlur**: motion blur
- **HighQualityLightShafts**: higher resolution light shafts (Light Shafts must be enabled in-game)
- **ShadowResolution**: shadow maps sharper than the Ultra setting (`2048`). `4096` is a good choice on recent GPUs.

All of them keep the game's own setting by default (`-1` or `0`).

## How It Works

InfinitePatch has two parts.

**`dinput8.dll`** sits in `Binaries\Win32`, where the game loads it in place of the Windows DirectInput library, which it forwards everything to. It reads `InfinitePatch.ini` when the game starts and applies the native fixes in memory only: the mouse handling for high polling rate mice and the sprint sensitivity fix. The game's files on disk are never changed.

**`InfinitePatch.ps1`** is a PowerShell script, run by `Install.bat` and `Uninstall.bat`. It reads `InfinitePatch.ini` and edits:

| Location | Changes |
| --- | --- |
| `Documents\My Games\BioShock Infinite\XGame\Config\XEngine.ini` | Display mode, resolution, VSync, framerate limiter, intro videos, controllers, graphics tweaks |
| `Documents\My Games\BioShock Infinite\XGame\Config\XUserOptions.ini` | Display mode, resolution, VSync, FOV slider range |
| `Documents\My Games\BioShock Infinite\XGame\Config\XUI.ini` | UI scaling |
| Registry: `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` | GPU preference and windowed optimizations for `BioShockInfinite.exe` |

The Epic Games Store version uses `Documents\My Games\BioShock Infinite Epic` and is detected automatically.

On the first install, the original files and registry value are saved to `XGame\InfinitePatch-backup`. `Uninstall.bat` puts them back.

## Troubleshooting

**The game crashes on startup**
Close overlay and monitoring tools before launching the game: MSI Afterburner / RivaTuner Statistics Server, the NVIDIA overlay and the Discord overlay.

**My settings went back to default**
The game rebuilds its settings files after an update or a file verification on Steam. Run `Install.bat` again.

**"Game settings not found"**
Launch the game once, close it, then run `Install.bat` again.

**The camera spins on its own, or I see Xbox buttons with a keyboard and mouse**
Set `DisableControllers = 1` in `InfinitePatch.ini`, then run `Install.bat` again.

**I changed the display mode in the game's options**
The game's own options still work and take priority until you run `Install.bat` again.

**`Install.bat` says `dinput8.dll` belongs to another mod**
Another mod replaced InfinitePatch's `dinput8.dll`. Remove that mod, then extract the InfinitePatch zip again.

## Building from Source

The native part is built with Visual Studio 2022 or later and its *Desktop development with C++* workload. Run `build.bat`: it produces `Binaries\Win32\dinput8.dll` (32-bit, no runtime to install).

## Credits

- [PCGamingWiki](https://www.pcgamingwiki.com/wiki/BioShock_Infinite) for documenting many of the game's configuration settings.
- The [Godot engine team](https://godotengine.org/article/fixing-high-polling-rate-mice-on-windows/) for documenting the high polling rate mouse problem and its fix.
- [bioshock-infinite-headtracking](https://github.com/itsloopyo/bioshock-infinite-headtracking) by itsloopyo (MIT) for the game's function addresses and engine object layout used by the sprint sensitivity fix.
- Inspired by [MadnessPatch](https://github.com/Wemino/MadnessPatch) by Wemino.

BioShock Infinite is a trademark of Take-Two Interactive Software, Inc. This project is not affiliated with or endorsed by 2K Games or Take-Two Interactive.

## License

[MIT](LICENSE)
