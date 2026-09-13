# Safer Saving SKSE

Skyrim has a few issues that can make saves unstable or cause them to become corrupted, such a saving during a heavy
script load.
This mod tries to minimize the chances of that happening for longer running playthroughs.

## When saving is blocked

- In combat
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
- For 30 seconds after a loading screen, and after loading a save or starting a new game
- Before the player's surroundings have finished loading in
- With the inventory, a container, barter, gift, magic, favourites, dialogue, book, crafting,
  lockpicking, level-up, training, sleep/wait, mist or a loading screen open

The journal and the save/load menus are never blocked, so a deliberate manual save is always
available when you are standing still.

## Automatic saving

Every 15 minutes the mod saves for you. Only time you actually spend playing counts towards
that: the timer stops while the game is paused, while a menu that pauses the game is open, and
during loading screens, and slowing or speeding up time does not change it.

The useful part is what happens when the 15 minutes are up at a bad moment. The save is not
skipped and it is not forced through. It waits, and happens on the first frame that saving is
allowed again by the same list above. If you are deep in a fight, the save lands the moment the
fight ends. There is no time limit on that wait and no fallback that saves anyway, because a
save taken at a bad moment is the thing this mod exists to prevent.

Saves go to `SaferSave_01` upwards, rotating through five files by default. Your own saves, the
vanilla `Autosave` slots and the quicksave are never touched, and the vanilla autosave settings
are left exactly as they are. You get one "Autosaving..." message in the corner when a save is
written; waiting is silent. Saving by any other means, including your own quicksave, restarts
the 15 minutes, so the mod will not save again right after you just did.

The slots are shared across characters, so a second playthrough rotates through the same files.

## Requirements

[SKSE](https://skse.silverlock.org/)

[Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Installing

Drop into your Skyrim install dir, or install the archive with your mod manager. Safe to uninstall at any time:
the mod stores nothing inside your saves, so removing it leaves behind only the `SaferSave_NN` files, which are
ordinary saves to keep or delete as you like.

## Configuration

Every check above can be turned off individually, the post-load wait can be changed or
disabled, and the automatic save interval and slot count can be changed or turned off. The
defaults match the list above, so the mod works without touching anything.

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

[AutoSave]
iIntervalMinutes = 30      # or 0 to turn automatic saving off
iSlots = 3
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
