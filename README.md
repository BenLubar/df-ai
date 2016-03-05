Watch Dwarf Fortress play itself!

Plugin for DFHack 0.42.06-alpha1.

Installation:

- copy the df-ai folder to dfhack/plugins/df-ai
- add `add_subdirectory(df-ai)` to dfhack/plugins/CMakeLists.custom.txt
- compile DFHack

Run `enable df-ai` in the dfhack console.

Does not handle already started forts, or resume from saved game.

[![xkcd 1223](http://imgs.xkcd.com/comics/dwarf_fortress.png "I may be the kind of person who wastes a year implementing a Turing-complete computer in Dwarf Fortress, but that makes you the kind of person who wastes ten more getting that computer to run Minecraft.")](http://xkcd.com/1223/)


[![Join the chat at https://gitter.im/BenLubar/df-ai](https://badges.gitter.im/BenLubar/df-ai.svg)](https://gitter.im/BenLubar/df-ai?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

**FAQ for AntB:** If you're getting an error about `Sheet` not being a member of `df::enums::stockpile_list::stockpile_list`, update `library/xml` to have at least this commit: https://github.com/DFHack/df-structures/commit/cf90ebe944e01e81cc7c321d1206b416c8974204 - to do so, you can run this command: `cd library/xml && git checkout master && git pull`
