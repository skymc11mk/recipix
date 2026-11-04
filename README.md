# Recipix
A small recipe viewer for Horizon (`.hmod`, wasm component, written in C).
Browse every item, click for its recipes, right-click or U for its uses; search by name, `@namespace`, `#tag`.
Uses Horizon's `recipes`, `browser` and `input` interfaces (horizon:mods@0.1.0, MIT). Original code; not affiliated with
JEI, Mojang, Microsoft or the Horizon project.
Build: `sh build.sh`.; no "+" place-recipe button.

Recipix 0.1.0 — a recipe viewer for Horizon.

What it does
- Lists every item in the game, including items added by mods and servers.
- Left-click an item to see how it is crafted. Right-click it (or press U) to see what it is used in. R shows recipes.
- A tab for each station: crafting table, furnace, blast furnace, smoker, campfire, stonecutter, smithing table, brewing stand.
- Back button, and search by name, @namespace and #tag.

Install
Put recipix-0.1.0.hmod in your Horizon mods folder, then open an inventory inside a world.

Notes
- Works with Horizon launchers that support .hmod mods.
- No "+" button to place recipes.
- Original code, MIT license. Not affiliated with JEI, Mojang, Microsoft or the Horizon project.
