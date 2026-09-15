# bottega.lol

Aim / ESP / chams suite for Roblox, driven by a clean external ImGui menu. The tool reads game state from outside the process (no injection) using the standard memory API and renders an overlay above the game window.

[![Discord](https://img.shields.io/badge/Discord-Join%20the%20server-5865F2)](https://discord.gg/shvMwDHFF9)

---

## Join the Discord for updated offsets and updates

Offsets go stale every time Roblox ships a client build. Whenever an update breaks the project, grab the fresh offsets for your client build **in the Discord**. [**https://discord.gg/shvMwDHFF9**](https://discord.gg/shvMwDHFF9)

---

## Features

### Aimbot
- Aimbot with FOV circle (solid / dashed / dotted / filled), target indicator, line-to-target and custom crosshair
- **Silent Aim** — methods: Viewport, Mouse, Raycast, Magic Bullet, Phantom Forces
- Team check, target team/knocked filtering, max distance
- Mouse & Camera aim methods with smoothing and prediction
- Auto-shoot / magic-bullet force-key logic

### ESP
- Box ESP — Full / Corner / 3D styles, gradient, filled gradient, glow; bounding by parts or loaded mesh
- Skeleton, head dot, look direction, visibility check, outline flags
- Snap lines, ground circle, off-screen arrows, radar, player info box, player counter
- Damage indicators and avatar outlines
- Extra flags: Health / Armor / Money / Weapon / Distance / Knocked / Reloading / Scoped / Bot / Friend

### Chams
- Engine Chams — Default, Ghost, Wireframe, Colored Frame, Colored, Smoke (No Shadow), Smoke, Invisible; ghost palettes
- DX / Shader chams — DX Flat, DX Modes, CPU Shader; styles incl. soft breath, pulse wave, flow ribbon, neon swirl
- Occluded / outline / glow shaders, local-player offset toggle

### World
- Lighting control — Ambient, Outdoor Ambient, Fog, Brightness, Exposure, Clock Time, **No Shadows**
- Atmosphere — density, glare, haze, offset
- Particles — snow / rain / ash / fireflies with count, speed, wind, glow
- Custom camera FOV

### Visuals
- Bullet tracers — Line / Beam / Laser with thickness, lifetime and color
- Hitmarker — Cross / Circle / Dots, damage text, headshot color
- Hitsound — Bell / Skeet / Bubble / Click with volume

### Misc
- Movement — **Fly** (Velocity / Teleport / Phantom modes), Walk Speed, Noclip, Jump Power, Third Person
- Kill effects — Explosion / Confetti / Sparks / Smoke with particle controls
- Hitbox expander — size, no-collide, transparent, visualize
- Explorer overlay

### Settings
- UI scale, monitor-DPI matching, background blur
- Overlays — watermark, player list, player bar, Spotify, active hotkeys, explorer

---

## Requirements

- Windows 10 / 11 (x64)
- Visual Studio 2022 with the **Desktop development with C++** workload
- Roblox (the RobloxPlayerBeta.exe game process)
- A functional Roblox `version-4310300497aa4917` client build (see **Offsets** below)

## Building

1. Clone the repository.
2. Open `bottega.lol.sln` in Visual Studio 2022.
3. Set configuration to **Release / x64**.
4. Build the solution.

Output:
```
output\bottega.lol.exe            <-- external menu / controller
```

## Usage

> Run the menu **after** the Roblox game window is open (in-game, not just in the lobby).

1. Start Roblox and join a game.
2. Run `output\bottega.lol.exe`.
3. Press **INSERT** to toggle the menu. Press **HOME** to toggle the explorer overlay.

## Architecture

```
┌─────────────────────────────┐
│  bottega.lol.exe (external) │
│  ImGui overlay menu         │
│  aimbot / esp / chams       │
│  movement / world control   │
└─────────────────────────────┘
```

The tool reads game state through `ReadProcessMemory` / `WriteProcessMemory` (players, camera, world, mesh caches) and runs the aimbot, movement and menu logic externally. The overlay syncs to the game's client rect and renders with DirectX 11. No code is injected into the game process.

## Offsets

Class-property offsets live in:

- `bottega.lol\src\sdk\offsets.h` — the menu's offset set (82 namespaces, dump-derived + stable extras: render queues, mesh-cache layout, `Misc::StringLength`, camera aliases, …)

The current tree targets **Roblox `version-4310300497aa4917`**. Offsets drift with every Roblox update.

### Updating offsets after a Roblox patch

You don't need the dumper — fresh offsets are posted in the Discord after every client build.

1. Download the raw `offsets.h` for your Roblox version from the **Discord** (https://discord.gg/shvMwDHFF9).
2. Save it as `output\dumper\offsets.h` in the repo folder (create the folders if needed).
3. From the repo root run:

   ```powershell
   powershell -ExecutionPolicy Bypass -File output\merge_offsets.ps1
   ```

4. `merge_offsets.ps1` parses the raw offsets, rewrites them as `namespace Offsets`, re-applies the non-dumped extras (with stable fallbacks), regenerates the header, and prints the version + field count. If the header already matches your version it tells you. Rebuild the solution, done.

> Prefer not to build at all? **Join the Discord for updated offsets and updates**: https://discord.gg/shvMwDHFF9

## Disclaimer

This project is provided **for educational and research purposes only**. It is not affiliated with, endorsed by, or connected to Roblox Corporation. Using third-party software with Roblox violates the Roblox Terms of Service and carries a **risk of account termination**; use entirely at your own risk. You are responsible for how you use this code.