# Safer Saving SKSE

Skyrim has a few issues that can make saves unstable or cause them to become corrupted, such a saving during a heavy
script load.
This mod tries to minimize the chances of that happening for longer running playthroughs.

## When saving is blocked

- In combat
- With enemies nearby, the same test vanilla uses before it lets you wait or fast travel
- Moving, sprinting or sneaking
- Jumping or falling
- Swimming or flying
- Mid-attack, including bow draws, bashes and spellcasting
- Weapon or spell drawn, including the draw and sheathe animations
- Staggered, knocked down or ragdolled
- Bleeding out, unconscious or dying
- In a killmove
- Mounted
- Sitting, sleeping, or moving in or out of furniture
- In a paired or scripted animation
- During a scripted scene, when the game has taken your controls
- Holding an object with grab or telekinesis
- While a quest or a scene has turned waiting off
- Trespassing, or being warned to leave somewhere
- For 30 seconds after a loading screen, and after loading a save or starting a new game
- Before the player's surroundings have finished loading in
- With the inventory, a container, barter, gift, magic, favourites, dialogue, book, crafting,
  lockpicking, level-up, training, sleep/wait, mist or a loading screen open

The journal and the save/load menus are never blocked, so a deliberate manual save is always
available when you are standing still.

## Why won't it save?

Press the quicksave key while saving is blocked and a message in the corner tells you which check
stopped it, for example "Cannot save: In combat." Autosaves and saves made by scripts stay
silent, so nothing appears unless you asked to save. Turn the messages off with
`[Notification] bEnabled = false`. The "Cannot save:" lead-in is `[Notification] sPrefix`; a space
before the reason is added for you, and leaving it blank shows the reason on its own.

## Autosaves

The game stops making saves on its own: on pause, on fast travel, on waiting, on resting, and the
ones quests and mods ask for through scripts. In their place the mod takes its own save every
15 minutes of play, and only when saving is safe by the checks above. Set
`[Autosave] bDisableVanilla = false` to leave the game to its usual autosaves.

Only time the game is actually running counts toward the interval — it does not tick while paused,
in a menu that pauses the game, or on a loading screen. If the interval runs out while saving is
blocked, the save is not skipped: it waits and happens at the next safe moment, however long that
takes. Any save you make yourself restarts the interval. `[Autosave] iIntervalMinutes` changes the
interval, `0` turns it off, and `[Autosave] iSlots` sets how many slots it rotates through. The
saves show in the load menu as autosaves of the current character, numbered from 1001, so vanilla
autosaves and your own saves are never touched.

Four of the switches this changes (`bSaveOnPause`, `bSaveOnTravel`, `bSaveOnWait`, `bSaveOnRest`)
are the same four in the game's own Settings, Gameplay menu. They are put back every time you close
the journal, so changing them in-game will not stick while this is on. They live in
`SkyrimPrefs.ini`, which the game writes out itself, so turning the setting off again may not
restore them. Vanilla has all four on, if you need to put them back by hand.

## Requirements

[SKSE](https://skse.silverlock.org/)

[Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Installing

Drop into your Skyrim install dir, or install the archive with your mod manager. Safe to uninstall at any time.

## Configuration

Every check above can be turned off individually, and the post-load wait can be changed or
disabled. The defaults match the list above, so the mod works without touching anything.

Settings live in `Data/SKSE/Plugins/SaferSaving.ini`. Do not edit that file; it is overwritten
on update. Create `SaferSaving_custom.ini` beside it and copy in only the lines you want to
change:

```ini
[Movement]
bSneaking = false

[State]
bAnimationDriven = false   # allows saving while seated

[Load]
iSettleSeconds = 10

[Autosave]
bDisableVanilla = false    # keeps the game's own autosaves
iIntervalMinutes = 30      # save every 30 minutes instead of 15
```

Anything you leave out keeps its default.

## Building

```
git clone --recurse-submodules <this repo>
xmake f -m releasedbg -y
xmake package
```

That produces `build/packages/SaferSaving-<version>.zip`, laid out for a mod manager. To copy the
DLL straight into a mod folder on every build instead, set `XSE_TES5_MODS_PATH` to your mod
manager's mods directory before running `xmake`. Requires xmake 3.0 or newer and MSVC with C++23.

## License

Copyright (c) 2026 Ultimersiv

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 3, as published by the
Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
