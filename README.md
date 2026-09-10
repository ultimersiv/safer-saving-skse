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

## Requirements

[SKSE](https://skse.silverlock.org/)

[Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Installing

Drop `SaferSaving.dll` into `Data/SKSE/Plugins/`, or install the archive with your mod manager.

Safe to uninstall mid save by just deleting the DLL.

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
