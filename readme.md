# VR Editor

![VR Editor Preview](https://i.imgur.com/SepvEjq.gif)

[Video](https://www.youtube.com/watch?v=MLwao39cFjc)

---

## Core Features

### Enter Edit Mode
**Put your hand within any object and press the trigger twice to enter edit mode**  
*(Or configure a VRIK gesture)*

<details>
<summary>Preview</summary>

![Enter Edit Mode](https://i.imgur.com/J3spAcl.gif)

</details>

---

### Wheel Menu
**Hold B to bring up the Wheel Menu**

<details>
<summary>Preview</summary>

![Wheel Menu](https://i.imgur.com/gQwfFUt.gif)

</details>

---

### Move Objects
**Hold trigger while hovering over an object to move it**  
**While moving, use the thumbstick to rotate & change distance**

<details>
<summary>Preview</summary>

![Move Objects](https://i.imgur.com/4b2ypgA.gif)

</details>

---

### Scale Objects
**Hold X to make the thumbstick scale instead**

<details>
<summary>Preview</summary>

![Scale Objects](https://i.imgur.com/G8k2JM9.gif)

</details>

---

### Snap to Ground
**Press B while moving to snap the object to the ground**

<details>
<summary>Preview</summary>

![Snap to Ground](https://i.imgur.com/0FPSB0s.gif)

</details>

---

### Multi-Select
**Hold A and press trigger to select multiple objects**

<details>
<summary>Preview</summary>

![Multi Select](https://i.imgur.com/SepvEjq.gif)

</details>

---

### Precision Rotation
**Hold left trigger while moving an object to precisely rotate it**

<details>
<summary>Preview</summary>

![Precision Rotate](https://i.imgur.com/w21zWYs.gif)

</details>

---

### No-Collision Selection Mode
**Press right thumbstick to switch to a selection mode that lets you select objects without collision (such as banners and flowers)**

<details>
<summary>Preview</summary>

![No Collision Select](https://i.imgur.com/nBnQQ5I.gif)

</details>

---

### Gallery System
**Save any object to your gallery & place it from anywhere**

<details>
<summary>Preview</summary>

![Gallery](https://i.imgur.com/NEEUO2X.gif)

</details>

---

### Undo / Redo
**Double-tap A to undo & double-tap B to redo**  
*(Or use the buttons in the wheel menu)*

---

## Keymap

Much of this functionality is also available through the wheel menu.

![Keymap](https://i.imgur.com/i1vLcPV.png)

---

## Requirements

- [Base Object Swapper](https://www.nexusmods.com/skyrimspecialedition/mods/60805)  
- [Skyrim VR Tools](https://www.nexusmods.com/skyrimspecialedition/mods/27782)  
- [Address Library for VR](https://www.nexusmods.com/skyrimspecialedition/mods/58101)  
- [3DUI](https://www.nexusmods.com/skyrimspecialedition/mods/169497)  
- [Skyrim VR ESL Support](https://www.nexusmods.com/skyrimspecialedition/mods/106712)

---

## Beta

This mod is still in **Beta** until sufficient user feedback has been gathered.

---

## Compatibility

Compatible with the vast majority of mods
Some mods that change all arrow projectiles at the engine level might be incompatible
Spell Bender VR is compatible

---

## Stability, Safety & Uninstalling

Since this is a **.dll mod**, it can be uninstalled at any point.

- Most edits remain, as they are tied to your save file  
- Changed and added objects are stored in your save  
- No save bloat observed during testing  

**Stress test:**  
Spawning **20,000 tables** increased save size by **< 500 KB**

---

## Saving & Storing Edits

### How are edits saved?
Edits are stored whenever you normally save your game.

### Where are edits stored?

- **Pre-existing objects** (vanilla or modded):  
  Stored per-cell in Base Object Swapper–compatible INI files  
  Example:  `Data\VREditor_SolitudeOrigin_SWAP.ini`
- **Duplicated or gallery-placed objects:**  
Stored directly in the save file

---

## For Modders

If you want to share your creations, you have two main options:

### Base Object Swapper

**The easiest way to share edits**

- Share the generated Base Object Swapper INI files  
- Anyone with Base Object Swapper installed will see your changes

**Limitations:**
- Only supports moving & deleting pre-existing objects  
- Does **not** include duplicated or gallery-placed objects  
- Files update after restarting the game once post-edit

---

### ESP Patching with Spriggit

You can apply edits directly to an ESP.

- Partial Spriggit YAML files are generated per edited cell  
- You must merge them into your ESP’s Spriggit data manually  
- Requires a custom script to:
- Merge cell YAML files  
- Generate appropriate FormIDs  

Example path: `VREditor\spriggit-partials\Cells\6\8\SolitudeWinkingSkeever - 016A0E_Skyrim.esm\RecordData.partial.yaml`


⚠️ You must enable YAML generation in the MCM.

If you need help creating the merge script, just ask.

---

## Thanks & Credits

- **Shizof** — Engine wizardry behind Spell Wheel, which was a great inspiration for this mod. And extra thanks & credits for letting me use the super cool swirling orb thingy effect
- **RavenKZP** — Inspiration from In-Game Patcher  
- **Icons8** — UI icons
