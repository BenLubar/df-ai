**Notice:** df-ai is being rewritten to work as a C++ plugin with (hopefully) fewer quirks. The last ruby version is still available on the `ruby` branch of this repository.

Watch Dwarf Fortress play itself!

Plugin for DFHack 0.40.24-r5.

Installation:

- copy the df-ai folder to dfhack/plugins/df-ai
- add `add_subdirectory(df-ai)` to dfhack/plugins/CMakeLists.custom.txt
- compile DFHack

Start a fresh embark on an area with no aquifer, and with the caverns not too close to the surface. For best results, choose a site with a river on the west side.

Run `enable df-ai` in the dfhack console. <!--A few flags can be set **before the AI is started** to alter the AI:

- `rb_eval "$NO_QUIT = true"` makes the AI not quit after the game ends.
- `rb_eval "$AI_RANDOM_EMBARK = true"` makes the AI embark in a random location on a new world, then embark on the same world when it loses until it runs out of spots.
- `rb_eval "$DEBUG = true"` makes the AI print a lot of information about what it's doing to the dfhack console.
- `rb_eval "$RECORD_MOVIE = true"` makes the AI record a CMV of the game.-->

Does not handle already started forts, or resume from saved game.

[![xkcd 1223](http://imgs.xkcd.com/comics/dwarf_fortress.png "I may be the kind of person who wastes a year implementing a Turing-complete computer in Dwarf Fortress, but that makes you the kind of person who wastes ten more getting that computer to run Minecraft.")](http://xkcd.com/1223/)
