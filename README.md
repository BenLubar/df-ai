Watch Dwarf Fortress play itself!

Plugin for DFHack.

**[Download a pre-built version from the releases page.](https://github.com/BenLubar/df-ai/releases)**

Run `enable df-ai` in the dfhack console to start. Run `help ai` for a list of commands.

Does not handle already started forts, but can resume from df-ai saves. Check `dfhack-config/df-ai.json` for settings.

Compilation:

- copy the df-ai folder to dfhack/plugins/df-ai
- add `add_subdirectory(df-ai)` to dfhack/plugins/CMakeLists.custom.txt
- compile DFHack

[![xkcd 1223](http://imgs.xkcd.com/comics/dwarf_fortress.png "I may be the kind of person who wastes a year implementing a Turing-complete computer in Dwarf Fortress, but that makes you the kind of person who wastes ten more getting that computer to run Minecraft.")](http://xkcd.com/1223/)
