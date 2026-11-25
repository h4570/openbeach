# Presentation script API versions

Declare `"api": 1` in a presentation mod that ships Lua. The game refuses mods requiring a higher version than it supports.
Presentation mods with only data can omit the field.

| Version | Changes and migration |
|---|---|
| 1 | First version. See the [Lua API](scripting.md), [settings fields](mods.md#settings) and [named actions](actions.md). |

JSON and DSL file formats have their own versions. See the [format schemas](formats.md).
