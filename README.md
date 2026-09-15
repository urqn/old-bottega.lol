# bottega.lol — old / legacy build

> ⚠️ **This is the OLD bottega.lol.** The first attempt, built from scratch. It is messy, error-prone, and full of issues — it was dropped so the project can be **rebuilt from scratch on a clean base** next time.

[![Discord](https://img.shields.io/badge/Discord-Join%20the%20server-5865F2)](https://discord.gg/shvMwDHFF9)

---

## Join the Discord for updated offsets and updates

Offsets go stale every time Roblox ships a client build. Whenever an update breaks the project, grab the fresh offsets for your client build **in the Discord**. [**https://discord.gg/shvMwDHFF9**](https://discord.gg/shvMwDHFF9)

---

## What this build is

Old external-only version: an external ImGui overlay (aimbot / ESP / chams / world / visuals / misc) that reads game state with `ReadProcessMemory` / `WriteProcessMemory` and draws over the game window. No code is injected into the game process.

It is **not** the final product. The plan for the next version is to start from a proper base instead of this.

## Building

1. Clone the repository.
2. Open `bottega.lol.sln` in Visual Studio 2022.
3. Set configuration to **Release / x64**.
4. Build the solution.

Output:
```
output\bottega.lol.exe
```

## Usage

> Run the menu **after** the Roblox game window is open (in-game, not just in the lobby).

1. Start Roblox and join a game.
2. Run `output\bottega.lol.exe`.
3. Press **INSERT** to toggle the menu. Press **HOME** to toggle the explorer overlay.

## Offsets

The current tree targets **Roblox `version-4310300497aa4917`** with offsets in `bottega.lol\src\sdk\offsets.h`.

### Updating offsets after a Roblox patch

1. Download the raw `offsets.h` for your Roblox version from the **Discord** (https://discord.gg/shvMwDHFF9).
2. Save it as `output\dumper\offsets.h` in the repo folder (create the folders if needed).
3. From the repo root run:

   ```powershell
   powershell -ExecutionPolicy Bypass -File output\merge_offsets.ps1
   ```

4. The script rewrites `bottega.lol\src\sdk\offsets.h`, prints the version + field count, and tells you if it was already up to date. Rebuild the solution.

> **Join the Discord for updated offsets and updates**: https://discord.gg/shvMwDHFF9

## Disclaimer

This project is provided **for educational and research purposes only**. It is not affiliated with, endorsed by, or connected to Roblox Corporation. Using third-party software with Roblox violates the Roblox Terms of Service and carries a **risk of account termination**; use entirely at your own risk. You are responsible for how you use this code.