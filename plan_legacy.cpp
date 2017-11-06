#include "ai.h"
#include "plan.h"

#include "modules/Buildings.h"
#include "modules/Maps.h"

#include "df/block_square_event_grassst.h"
#include "df/buildings_other_id.h"
#include "df/map_block.h"
#include "df/tile_designation.h"
#include "df/tile_occupancy.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

const static int16_t MinX = -48, MinY = -22, MinZ = -6;
const static int16_t MaxX = 48, MaxY = 22, MaxZ = 1;

const static int32_t extra_farms = 3; // built after utilities are finished
const static int32_t spare_bedroom = 3; // dig this much free bedroom in advance when idle
const static int32_t dwarves_per_table = 3; // number of dwarves per dininghall table/chair
const static int32_t dwarves_per_farmtile_num = 3; // number of dwarves per farmplot tile
const static int32_t dwarves_per_farmtile_den = 2;
const static int16_t farm_w = 3;
const static int16_t farm_h = 3;
const static int32_t dpf = farm_w * farm_h * dwarves_per_farmtile_num / dwarves_per_farmtile_den;
const static int32_t nrfarms = (220 + dpf - 1) / dpf + extra_farms;

static furniture *new_furniture(layout_type::type type, int16_t x, int16_t y)
{
    furniture *f = new furniture();
    f->type = type;
    f->pos.x = x;
    f->pos.y = y;
    return f;
}

static furniture *new_furniture_with_users(layout_type::type type, int16_t x, int16_t y, int32_t users_count, bool ignore = false)
{
    furniture *f = new_furniture(type, x, y);
    f->has_users = users_count;
    f->ignore = ignore;
    return f;
}

static furniture *new_cage_trap(int16_t x, int16_t y)
{
    furniture *f = new_furniture(layout_type::cage_trap, x, y);
    f->ignore = true;
    return f;
}

static furniture *new_door(int16_t x, int16_t y, bool internal = false)
{
    furniture *f = new_furniture(layout_type::door, x, y);
    f->internal = internal;
    return f;
}

static furniture *new_dig(df::tile_dig_designation d, int16_t x, int16_t y, int16_t z = 0)
{
    furniture *f = new furniture();
    f->dig = d;
    f->pos.x = x;
    f->pos.y = y;
    f->pos.z = z;
    return f;
}

static furniture *new_hive_floor(int16_t x, int16_t y)
{
    furniture *f = new_furniture(layout_type::hive, x, y);
    f->construction = construction_type::Floor;
    return f;
}

static furniture *new_construction(df::construction_type c, int16_t x, int16_t y, int16_t z = 0)
{
    furniture *f = new furniture();
    f->construction = c;
    f->pos.x = x;
    f->pos.y = y;
    f->pos.z = z;
    return f;
}

static furniture *new_wall(int16_t x, int16_t y, int16_t z = 0)
{
    furniture *f = new_construction(construction_type::Wall, x, y, z);
    f->dig = tile_dig_designation::No;
    return f;
}

static furniture *new_well(int16_t x, int16_t y)
{
    furniture *f = new_furniture(layout_type::well, x, y);
    f->dig = tile_dig_designation::Channel;
    return f;
}

static furniture *new_cistern_lever(int16_t x, int16_t y, const std::string & comment, bool internal)
{
    furniture *f = new_furniture(layout_type::lever, x, y);
    f->comment = comment;
    f->internal = internal;
    return f;
}

static furniture *new_cistern_floodgate(int16_t x, int16_t y, const std::string & comment, bool internal, bool ignore = false)
{
    furniture *f = new_furniture(layout_type::floodgate, x, y);
    f->comment = comment;
    f->internal = internal;
    f->ignore = ignore;
    return f;
}

command_result Plan::setup_ready(color_ostream & out)
{
    auto dig_starting_room = [this, &out](room_type::type rt, std::function<bool(room *)> f, bool want = false)
    {
        find_room(rt, [this, &out, f, want](room *r) -> bool
        {
            if (f(r))
            {
                if (want)
                {
                    wantdig(out, r);
                }
                else
                {
                    digroom(out, r);
                }
                return true;
            }
            return false;
        });
    };

    dig_starting_room(room_type::workshop, [](room *r) -> bool { return r->workshop_type == workshop_type::Masons && r->level == 0; });
    dig_starting_room(room_type::workshop, [](room *r) -> bool { return r->workshop_type == workshop_type::Carpenters && r->level == 0; });
    dig_starting_room(room_type::stockpile, [](room *r) -> bool { return r->stockpile_type == stockpile_type::food && r->level == 0 && r->workshop && r->workshop->type == room_type::farmplot; }, true);

    dig_starting_room(room_type::garbagedump, [](room *) -> bool { return true; });

    return CR_OK;
}

command_result Plan::setup_blueprint_legacy(color_ostream & out)
{
    if (!config.plan_allow_legacy)
    {
        ai->debug(out, "not allowed to use legacy plan and blueprint failed. giving up.");
        return CR_FAILURE;
    }

    ai->debug(out, "using legacy layout...");

    // TODO use existing fort facilities (so we can relay the user or continue from a save)
    ai->debug(out, "setting up fort blueprint...");
    // TODO place fort body first, have main stair stop before surface, and place trade depot on path to surface
    command_result res = scan_fort_entrance(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "blueprint found entrance");
    // TODO if no room for fort body, make surface fort
    res = scan_fort_body(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "blueprint found body");
    res = setup_blueprint_rooms(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "blueprint found rooms");
    // ensure traps are on the surface
    for (auto i : fort_entrance->layout)
    {
        i->pos.z = surface_tile_at(fort_entrance->min.x + i->pos.x, fort_entrance->min.y + i->pos.y, true).z - fort_entrance->min.z;
    }
    fort_entrance->layout.erase(std::remove_if(fort_entrance->layout.begin(), fort_entrance->layout.end(), [this](furniture *i) -> bool
    {
        df::coord t = fort_entrance->min + i->pos + df::coord(0, 0, -1);
        df::tiletype *tt = Maps::getTileType(t);
        if (!tt)
        {
            delete i;
            return true;
        }
        df::tiletype_material tm = ENUM_ATTR(tiletype, material, *tt);
        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Wall || (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && (!allow_ice || tm != tiletype_material::FROZEN_LIQUID)))
        {
            delete i;
            return true;
        }
        return false;
    }), fort_entrance->layout.end());

    categorize_all();

    return setup_ready(out);
}

// search a valid tile for fortress entrance
command_result Plan::scan_fort_entrance(color_ostream & out)
{
    // map center
    int16_t cx = world->map.x_count / 2;
    int16_t cy = world->map.y_count / 2;
    df::coord center = surface_tile_at(cx, cy, true);

    df::coord ent0 = spiral_search(center, [this](df::coord t0) -> bool
    {
        // test the whole map for 3x5 clear spots
        df::coord t = surface_tile_at(t0.x, t0.y);
        if (!t.isValid())
            return false;

        // make sure we're not too close to the edge of the map.
        if (t.x + MinX < 0 || t.x + MaxX >= world->map.x_count ||
            t.y + MinY < 0 || t.y + MaxY >= world->map.y_count ||
            t.z + MinZ < 0 || t.z + MaxZ >= world->map.z_count)
        {
            return false;
        }

        for (int16_t _x = -1; _x <= 1; _x++)
        {
            for (int16_t _y = -2; _y <= 2; _y++)
            {
                df::tiletype tt = *Maps::getTileType(t + df::coord(_x, _y, -1));
                if (ENUM_ATTR(tiletype, shape, tt) != tiletype_shape::WALL)
                    return false;
                df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
                if (!allow_ice &&
                    tm != tiletype_material::STONE &&
                    tm != tiletype_material::MINERAL &&
                    tm != tiletype_material::SOIL &&
                    tm != tiletype_material::ROOT)
                    return false;
                df::coord ttt = t + df::coord(_x, _y, 0);
                if (ENUM_ATTR(tiletype, shape, *Maps::getTileType(ttt)) != tiletype_shape::FLOOR)
                    return false;
                df::tile_designation td = *Maps::getTileDesignation(ttt);
                if (td.bits.flow_size != 0 || td.bits.hidden)
                    return false;
                if (Buildings::findAtTile(ttt))
                    return false;
            }
        }
        for (int16_t _x = -3; _x <= 3; _x++)
        {
            for (int16_t _y = -4; _y <= 4; _y++)
            {
                if (!surface_tile_at(t.x + _x, t.y + _y, true).isValid())
                    return false;
            }
        }
        return true;
    });

    if (!ent0.isValid())
    {
        if (!allow_ice)
        {
            allow_ice = true;

            return scan_fort_entrance(out);
        }

        ai->debug(out, "[ERROR] Can't find a fortress entrance spot. We need a 3x5 flat area with solid ground for at least 2 tiles on each side.");
        return CR_FAILURE;
    }

    df::coord ent = surface_tile_at(ent0.x, ent0.y);

    fort_entrance = new room(corridor_type::corridor, ent - df::coord(0, 1, 0), ent + df::coord(0, 1, 0), "main staircase - fort entrance");
    for (int i = 0; i < 3; i++)
    {
        fort_entrance->layout.push_back(new_cage_trap(i - 1, -1));
        fort_entrance->layout.push_back(new_cage_trap(1, i));
        fort_entrance->layout.push_back(new_cage_trap(1 - i, 3));
        fort_entrance->layout.push_back(new_cage_trap(-1, 2 - i));
    }
    for (int i = 0; i < 5; i++)
    {
        fort_entrance->layout.push_back(new_cage_trap(i - 2, -2));
        fort_entrance->layout.push_back(new_cage_trap(2, i - 1));
        fort_entrance->layout.push_back(new_cage_trap(2 - i, 4));
        fort_entrance->layout.push_back(new_cage_trap(-2, 3 - i));
    }

    return CR_OK;
}

// search how much we need to dig to find a spot for the full fortress body
// here we cheat and work as if the map was fully reveal()ed
command_result Plan::scan_fort_body(color_ostream & out)
{
    // use a hardcoded fort layout
    df::coord c = fort_entrance->pos();

    for (int16_t cz1 = c.z; cz1 >= 0; cz1--)
    {
        bool stop = false;
        // stop searching if we hit a cavern or an aquifer inside our main
        // staircase
        for (int16_t x = fort_entrance->min.x; !stop && x <= fort_entrance->max.x; x++)
        {
            for (int16_t y = fort_entrance->min.y; !stop && y <= fort_entrance->max.y; y++)
            {
                df::coord t(x, y, cz1 + MaxZ);
                if (!map_tile_nocavern(t) || Maps::getTileDesignation(t)->bits.water_table)
                    stop = true;
            }
        }
        if (stop)
        {
            break;
        }

        auto check = [this, c, cz1, &stop](int16_t dx, int16_t dy, int16_t dz)
        {
            df::coord t = df::coord(c, cz1) + df::coord(dx, dy, dz);
            df::tiletype tt = *Maps::getTileType(t);
            df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
            if (ENUM_ATTR(tiletype, shape, tt) != tiletype_shape::WALL ||
                Maps::getTileDesignation(t)->bits.water_table ||
                (tm != tiletype_material::STONE &&
                    tm != tiletype_material::MINERAL &&
                    (!allow_ice || tm != tiletype_material::FROZEN_LIQUID) &&
                    (dz < 0 || (tm != tiletype_material::SOIL &&
                        tm != tiletype_material::ROOT))))
                stop = true;
        };

        for (int16_t dz = MinZ; !stop && dz <= MaxZ; dz++)
        {
            // scan perimeter first to quickly eliminate caverns / bad rock
            // layers
            for (int16_t dx = MinX; !stop && dx <= MaxX; dx++)
            {
                for (int16_t dy = MinY; !stop && dy <= MaxY; dy += MaxY - MinY)
                {
                    check(dx, dy, dz);
                }
            }
            for (int16_t dx = MinX; !stop && dx <= MaxX; dx += MaxX - MinX)
            {
                for (int16_t dy = MinY + 1; !stop && dy <= MaxY - 1; dy++)
                {
                    check(dx, dy, dz);
                }
            }
            // perimeter ok, full scan
            for (int16_t dx = MinX + 1; !stop && dx <= MaxX - 1; dx++)
            {
                for (int16_t dy = MinY + 1; !stop && dy <= MaxY - 1; dy++)
                {
                    check(dx, dy, dz);
                }
            }
        }

        if (!stop)
        {
            fort_entrance->min.z = cz1;
            return CR_OK;
        }
    }

    ai->debug(out, "[ERROR] Too many caverns, cant find room for fort. We need more minerals!");
    return CR_FAILURE;
}

// assign rooms in the space found by scan_fort_*
command_result Plan::setup_blueprint_rooms(color_ostream & out)
{
    // hardcoded layout
    rooms_and_corridors.push_back(fort_entrance);

    df::coord f = fort_entrance->pos();

    command_result res;

    std::vector<room *> fe;
    fe.push_back(fort_entrance);

    f.z = fort_entrance->min.z;
    res = setup_blueprint_workshops(out, f, fe);
    if (res != CR_OK)
        return res;
    ai->debug(out, "workshop floor ready");

    fort_entrance->min.z--;
    f.z--;
    res = setup_blueprint_utilities(out, f, fe);
    if (res != CR_OK)
        return res;
    ai->debug(out, "utility floor ready");

    fort_entrance->min.z--;
    f.z--;
    res = setup_blueprint_stockpiles(out, f, fe);
    if (res != CR_OK)
        return res;
    ai->debug(out, "stockpile floor ready");

    for (int i = 0; i < 2; i++)
    {
        fort_entrance->min.z--;
        f.z--;
        res = setup_blueprint_bedrooms(out, f, fe, i);
        if (res != CR_OK)
            return res;
        ai->debug(out, stl_sprintf("bedroom floor ready %d/2", i + 1));
    }

    fort_entrance->min.z--;
    f.z--;
    res = setup_blueprint_stockpiles_overflow(out, f, fe);
    if (res != CR_OK)
        return res;
    ai->debug(out, "stockpile overflow floor ready");

    return CR_OK;
}

command_result Plan::setup_blueprint_workshops(color_ostream &, df::coord f, const std::vector<room *> & entr)
{
    room *corridor_center0 = new room(corridor_type::corridor, f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0), "main staircase - west workshops");
    corridor_center0->layout.push_back(new_door(-1, 0));
    corridor_center0->layout.push_back(new_door(-1, 2));
    corridor_center0->accesspath = entr;
    rooms_and_corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(corridor_type::corridor, f + df::coord(1, -1, 0), f + df::coord(1, 1, 0), "main staircase - east workshops");
    corridor_center2->layout.push_back(new_door(1, 0));
    corridor_center2->layout.push_back(new_door(1, 2));
    corridor_center2->accesspath = entr;
    rooms_and_corridors.push_back(corridor_center2);

    // add minimal stockpile in front of workshop
    const static struct sptypes
    {
        typedef std::tuple<stockpile_type::type, std::set<df::stockpile_list>, bool, bool> definition_t;
        std::map<df::workshop_type, definition_t> workshops;
        std::map<df::furnace_type, definition_t> furnaces;
        sptypes()
        {
            std::set<df::stockpile_list> disable;
            disable.insert(stockpile_list::StoneOres);
            disable.insert(stockpile_list::StoneClay);
            workshops[workshop_type::Masons] = std::make_tuple(stockpile_type::stone, disable, false, false);
            disable.clear();
            workshops[workshop_type::Carpenters] = std::make_tuple(stockpile_type::wood, disable, false, false);
            disable.clear();
            disable.insert(stockpile_list::RefuseItems);
            disable.insert(stockpile_list::RefuseCorpses);
            disable.insert(stockpile_list::RefuseParts);
            workshops[workshop_type::Craftsdwarfs] = std::make_tuple(stockpile_type::refuse, disable, false, false);
            disable.clear();
            disable.insert(stockpile_list::FoodMeat);
            disable.insert(stockpile_list::FoodFish);
            disable.insert(stockpile_list::FoodUnpreparedFish);
            disable.insert(stockpile_list::FoodEgg);
            disable.insert(stockpile_list::FoodDrinkPlant);
            disable.insert(stockpile_list::FoodDrinkAnimal);
            disable.insert(stockpile_list::FoodCheesePlant);
            disable.insert(stockpile_list::FoodCheeseAnimal);
            disable.insert(stockpile_list::FoodSeeds);
            disable.insert(stockpile_list::FoodLeaves);
            disable.insert(stockpile_list::FoodMilledPlant);
            disable.insert(stockpile_list::FoodBoneMeal);
            disable.insert(stockpile_list::FoodFat);
            disable.insert(stockpile_list::FoodPaste);
            disable.insert(stockpile_list::FoodPressedMaterial);
            disable.insert(stockpile_list::FoodExtractPlant);
            disable.insert(stockpile_list::FoodMiscLiquid);
            workshops[workshop_type::Farmers] = std::make_tuple(stockpile_type::food, disable, true, false);
            disable.clear();
            disable.insert(stockpile_list::FoodMeat);
            disable.insert(stockpile_list::FoodFish);
            disable.insert(stockpile_list::FoodEgg);
            disable.insert(stockpile_list::FoodPlants);
            disable.insert(stockpile_list::FoodDrinkPlant);
            disable.insert(stockpile_list::FoodDrinkAnimal);
            disable.insert(stockpile_list::FoodCheesePlant);
            disable.insert(stockpile_list::FoodCheeseAnimal);
            disable.insert(stockpile_list::FoodSeeds);
            disable.insert(stockpile_list::FoodLeaves);
            disable.insert(stockpile_list::FoodMilledPlant);
            disable.insert(stockpile_list::FoodBoneMeal);
            disable.insert(stockpile_list::FoodFat);
            disable.insert(stockpile_list::FoodPaste);
            disable.insert(stockpile_list::FoodPressedMaterial);
            disable.insert(stockpile_list::FoodExtractPlant);
            disable.insert(stockpile_list::FoodExtractAnimal);
            disable.insert(stockpile_list::FoodMiscLiquid);
            workshops[workshop_type::Fishery] = std::make_tuple(stockpile_type::food, disable, true, false);
            disable.clear();
            disable.insert(stockpile_list::FoodFish);
            disable.insert(stockpile_list::FoodUnpreparedFish);
            disable.insert(stockpile_list::FoodEgg);
            disable.insert(stockpile_list::FoodPlants);
            disable.insert(stockpile_list::FoodDrinkPlant);
            disable.insert(stockpile_list::FoodDrinkAnimal);
            disable.insert(stockpile_list::FoodCheesePlant);
            disable.insert(stockpile_list::FoodCheeseAnimal);
            disable.insert(stockpile_list::FoodSeeds);
            disable.insert(stockpile_list::FoodLeaves);
            disable.insert(stockpile_list::FoodMilledPlant);
            disable.insert(stockpile_list::FoodBoneMeal);
            disable.insert(stockpile_list::FoodFat);
            disable.insert(stockpile_list::FoodPaste);
            disable.insert(stockpile_list::FoodPressedMaterial);
            disable.insert(stockpile_list::FoodExtractPlant);
            disable.insert(stockpile_list::FoodExtractAnimal);
            disable.insert(stockpile_list::FoodMiscLiquid);
            workshops[workshop_type::Butchers] = std::make_tuple(stockpile_type::food, disable, true, false);
            disable.clear();
            workshops[workshop_type::Jewelers] = std::make_tuple(stockpile_type::gems, disable, false, false);
            disable.clear();
            disable.insert(stockpile_list::ClothSilk);
            disable.insert(stockpile_list::ClothPlant);
            disable.insert(stockpile_list::ClothYarn);
            disable.insert(stockpile_list::ClothMetal);
            workshops[workshop_type::Loom] = std::make_tuple(stockpile_type::cloth, disable, false, false);
            disable.clear();
            disable.insert(stockpile_list::ThreadSilk);
            disable.insert(stockpile_list::ThreadPlant);
            disable.insert(stockpile_list::ThreadYarn);
            disable.insert(stockpile_list::ThreadMetal);
            workshops[workshop_type::Clothiers] = std::make_tuple(stockpile_type::cloth, disable, false, false);
            disable.clear();
            disable.insert(stockpile_list::FoodMeat);
            disable.insert(stockpile_list::FoodFish);
            disable.insert(stockpile_list::FoodUnpreparedFish);
            disable.insert(stockpile_list::FoodEgg);
            disable.insert(stockpile_list::FoodDrinkPlant);
            disable.insert(stockpile_list::FoodDrinkAnimal);
            disable.insert(stockpile_list::FoodCheesePlant);
            disable.insert(stockpile_list::FoodCheeseAnimal);
            disable.insert(stockpile_list::FoodSeeds);
            disable.insert(stockpile_list::FoodMilledPlant);
            disable.insert(stockpile_list::FoodBoneMeal);
            disable.insert(stockpile_list::FoodFat);
            disable.insert(stockpile_list::FoodPaste);
            disable.insert(stockpile_list::FoodPressedMaterial);
            disable.insert(stockpile_list::FoodExtractPlant);
            disable.insert(stockpile_list::FoodExtractAnimal);
            disable.insert(stockpile_list::FoodMiscLiquid);
            workshops[workshop_type::Still] = std::make_tuple(stockpile_type::food, disable, true, false);
            disable.clear();
            disable.insert(stockpile_list::FoodDrinkPlant);
            disable.insert(stockpile_list::FoodDrinkAnimal);
            disable.insert(stockpile_list::FoodSeeds);
            disable.insert(stockpile_list::FoodBoneMeal);
            disable.insert(stockpile_list::FoodMiscLiquid);
            workshops[workshop_type::Kitchen] = std::make_tuple(stockpile_type::food, disable, true, false);
            disable.clear();
            furnaces[furnace_type::WoodFurnace] = std::make_tuple(stockpile_type::wood, disable, false, false);
            disable.clear();
            disable.insert(stockpile_list::StoneOther);
            disable.insert(stockpile_list::StoneClay);
            furnaces[furnace_type::Smelter] = std::make_tuple(stockpile_type::stone, disable, false, false);
            disable.clear();
            disable.insert(stockpile_list::BlocksStone);
            disable.insert(stockpile_list::BlocksMetal);
            disable.insert(stockpile_list::BlocksOther);
            workshops[workshop_type::MetalsmithsForge] = std::make_tuple(stockpile_type::bars_blocks, disable, false, false);
        }

        size_t count(room *r) const
        {
            if (r->type == room_type::workshop)
            {
                return workshops.count(r->workshop_type);
            }
            if (r->type == room_type::furnace)
            {
                return furnaces.count(r->furnace_type);
            }
            return 0;
        }

        const definition_t & at(room *r) const
        {
            if (r->type == room_type::workshop)
            {
                return workshops.at(r->workshop_type);
            }
            if (r->type == room_type::furnace)
            {
                return furnaces.at(r->furnace_type);
            }
            throw std::out_of_range("invalid room for sptypes::at");
        }
    } sptypes;

    // Millstone, Siege, magma workshops/furnaces
    std::vector<std::function<room*(df::coord, df::coord, const std::string &)>> types;
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Still, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Kitchen, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Fishery, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Butchers, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Leatherworks, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Tanners, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Loom, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Clothiers, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Dyers, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Bowyers, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Masons, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(furnace_type::Kiln, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Masons, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Carpenters, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Mechanics, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Farmers, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Craftsdwarfs, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Jewelers, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(furnace_type::Smelter, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::MetalsmithsForge, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(workshop_type::Ashery, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(furnace_type::WoodFurnace, min, max, comment); });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { room *r = new room(workshop_type::Custom, min, max, comment); r->raw_type = "SOAP_MAKER"; return r; });
    types.push_back([](df::coord min, df::coord max, const std::string & comment) -> room * { return new room(furnace_type::GlassFurnace, min, max, comment); });
    auto type_it = types.begin();

    for (int16_t dirx = -1; dirx <= 1; dirx += 2)
    {
        room *prev_corx = dirx < 0 ? corridor_center0 : corridor_center2;
        int16_t ocx = f.x + dirx * 3;
        for (int16_t dx = 1; dx <= 6; dx++)
        {
            // segments of the big central horizontal corridor
            int16_t cx = f.x + dirx * 4 * dx;
            room *cor_x = new room(corridor_type::corridor, df::coord(ocx, f.y - 1, f.z), df::coord(cx + (dx <= 5 ? 0 : dirx), f.y + 1, f.z), stl_sprintf("%s workshops - segment %d", dirx == 1 ? "east" : "west", dx));
            cor_x->accesspath.push_back(prev_corx);
            rooms_and_corridors.push_back(cor_x);
            prev_corx = cor_x;
            ocx = cx + dirx;

            if (dirx == 1 && dx == 3)
            {
                // stuff a quern&screwpress near the farmers'
                df::coord c(cx - 2, f.y + 1, f.z);
                room *r = new room(workshop_type::Quern, c, c);
                r->accesspath.push_back(cor_x);
                r->level = 0;
                rooms_and_corridors.push_back(r);

                c.x = cx - 6;
                r = new room(workshop_type::Quern, c, c);
                r->accesspath.push_back(cor_x);
                r->level = 1;
                rooms_and_corridors.push_back(r);

                c.x = cx + 2;
                r = new room(workshop_type::Quern, c, c);
                r->accesspath.push_back(cor_x);
                r->level = 2;
                rooms_and_corridors.push_back(r);

                c.x = cx - 2;
                c.y = f.y - 1;
                r = new room(workshop_type::Custom, c, c);
                r->raw_type = "SCREW_PRESS";
                r->accesspath.push_back(cor_x);
                r->level = 0;
                rooms_and_corridors.push_back(r);
            }
            else if (dirx == -1 && dx == 6)
            {
                room *r = new room(location_type::library, df::coord(cx - 12, f.y - 5, f.z), df::coord(cx - 3, f.y - 1, f.z));
                r->layout.push_back(new_door(10, 4));
                r->layout.push_back(new_furniture(layout_type::chest, 9, 3));
                r->layout.push_back(new_furniture(layout_type::chest, 9, 2));
                r->layout.push_back(new_furniture(layout_type::table, 9, 1));
                r->layout.push_back(new_furniture(layout_type::chair, 8, 1));
                r->layout.push_back(new_furniture(layout_type::table, 9, 0));
                r->layout.push_back(new_furniture(layout_type::chair, 8, 0));
                for (int16_t i = 0; i < 6; i++)
                {
                    r->layout.push_back(new_furniture(layout_type::bookcase, i + 1, 1));
                    r->layout.push_back(new_furniture(layout_type::bookcase, i + 1, 3));
                }
                r->accesspath.push_back(cor_x);
                rooms_and_corridors.push_back(r);

                r = new room(location_type::temple, df::coord(cx - 12, f.y + 1, f.z), df::coord(cx - 3, f.y + 5, f.z));
                r->layout.push_back(new_door(10, 0));
                r->accesspath.push_back(cor_x);
                rooms_and_corridors.push_back(r);
            }

            auto make_room = *type_it++;

            room *r = make_room(df::coord(cx - 1, f.y - 5, f.z), df::coord(cx + 1, f.y - 3, f.z), "");
            r->accesspath.push_back(cor_x);
            r->layout.push_back(new_door(1, 3));
            r->level = 0;
            if (dirx == -1 && dx == 1)
            {
                r->layout.push_back(new_furniture(layout_type::nest_box, -1, 4));
            }
            rooms_and_corridors.push_back(r);
            room *prev_room = r;

            if (sptypes.count(r) > 0)
            {
                auto stock = sptypes.at(r);
                room *sp = new room(std::get<0>(stock), df::coord(r->min.x, r->max.y + 2, r->min.z), df::coord(r->max.x, r->max.y + 2, r->min.z));
                sp->stock_disable = std::get<1>(stock);
                sp->stock_specific1 = std::get<2>(stock);
                sp->stock_specific2 = std::get<3>(stock);
                sp->workshop = r;
                sp->level = 0;
                prev_room->accesspath.push_back(sp);
                rooms_and_corridors.push_back(sp);
            }

            r = make_room(df::coord(cx - 1, f.y - 8, f.z), df::coord(cx + 1, f.y - 6, f.z), "");
            r->accesspath.push_back(prev_room);
            r->level = 1;
            rooms_and_corridors.push_back(r);
            prev_room = r;

            r = make_room(df::coord(cx - 1, f.y - 11, f.z), df::coord(cx + 1, f.y - 9, f.z), "");
            r->accesspath.push_back(prev_room);
            r->level = 2;
            rooms_and_corridors.push_back(r);
            prev_room = r;

            make_room = *type_it++;
            r = make_room(df::coord(cx - 1, f.y + 3, f.z), df::coord(cx + 1, f.y + 5, f.z), "");
            r->accesspath.push_back(cor_x);
            r->layout.push_back(new_door(1, -1));
            r->level = 0;
            prev_room = r;
            if (dirx == -1 && dx == 1)
            {
                r->layout.push_back(new_furniture(layout_type::nest_box, -1, -2));
            }
            rooms_and_corridors.push_back(r);

            if (sptypes.count(r))
            {
                auto stock = sptypes.at(r);
                room *sp = new room(std::get<0>(stock), df::coord(r->min.x, r->min.y - 2, r->min.z), df::coord(r->max.x, r->min.y - 2, r->min.z));
                sp->stock_disable = std::get<1>(stock);
                sp->stock_specific1 = std::get<2>(stock);
                sp->stock_specific2 = std::get<3>(stock);
                sp->workshop = r;
                sp->level = 0;
                prev_room->accesspath.push_back(sp);
                rooms_and_corridors.push_back(sp);
            }

            r = make_room(df::coord(cx - 1, f.y + 6, f.z), df::coord(cx + 1, f.y + 8, f.z), "");
            r->accesspath.push_back(prev_room);
            r->level = 1;
            rooms_and_corridors.push_back(r);
            prev_room = r;

            r = make_room(df::coord(cx - 1, f.y + 9, f.z), df::coord(cx + 1, f.y + 11, f.z), "");
            r->accesspath.push_back(prev_room);
            r->level = 2;
            rooms_and_corridors.push_back(r);
            prev_room = r;
        }
    }

    df::coord depot_center = spiral_search(df::coord(f.x - 4, f.y, fort_entrance->max.z - 1), [this](df::coord t) -> bool
    {
        for (int16_t dx = -2; dx <= 2; dx++)
        {
            for (int16_t dy = -2; dy <= 2; dy++)
            {
                df::coord tt = t + df::coord(dx, dy, 0);
                if (!map_tile_in_rock(tt))
                    return false;
                if (map_tile_intersects_room(tt))
                    return false;
            }
        }
        for (int16_t dy = -1; dy <= 1; dy++)
        {
            df::coord tt = t + df::coord(-3, dy, 0);
            if (!map_tile_in_rock(tt))
                return false;
            df::coord ttt = tt + df::coord(0, 0, 1);
            if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(ttt))) != tiletype_shape_basic::Floor)
                return false;
            if (map_tile_intersects_room(tt))
                return false;
            if (map_tile_intersects_room(ttt))
                return false;
            df::tile_occupancy *occ = Maps::getTileOccupancy(ttt);
            if (occ && occ->bits.building != tile_building_occ::None)
                return false;
        }
        return true;
    });

    if (depot_center.isValid())
    {
        room *r = new room(room_type::tradedepot, depot_center - df::coord(2, 2, 0), depot_center + df::coord(2, 2, 0));
        r->level = 0;
        r->layout.push_back(new_dig(tile_dig_designation::Ramp, depot_center.x < f.x ? -1 : 5, 1));
        r->layout.push_back(new_dig(tile_dig_designation::Ramp, depot_center.x < f.x ? -1 : 5, 2));
        r->layout.push_back(new_dig(tile_dig_designation::Ramp, depot_center.x < f.x ? -1 : 5, 3));
        rooms_and_corridors.push_back(r);
    }
    else
    {
        room *r = new room(room_type::tradedepot, df::coord(f.x - 7, f.y - 2, fort_entrance->max.z), df::coord(f.x - 3, f.y + 2, fort_entrance->max.z));
        r->level = 0;
        for (int16_t x = 0; x < 5; x++)
        {
            for (int16_t y = 0; y < 5; y++)
            {
                r->layout.push_back(new_construction(construction_type::Floor, x, y));
            }
        }
        rooms_and_corridors.push_back(r);
    }
    return CR_OK;
}

command_result Plan::setup_blueprint_stockpiles(color_ostream & out, df::coord f, const std::vector<room *> & entr)
{
    room *corridor_center0 = new room(corridor_type::corridor, f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0), "main staircase - west stockpiles");
    corridor_center0->layout.push_back(new_door(-1, 0));
    corridor_center0->layout.push_back(new_door(-1, 2));
    corridor_center0->accesspath = entr;
    rooms_and_corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(corridor_type::corridor, f + df::coord(1, -1, 0), f + df::coord(1, 1, 0), "main staircase - east stockpiles");
    corridor_center2->layout.push_back(new_door(1, 0));
    corridor_center2->layout.push_back(new_door(1, 2));
    corridor_center2->accesspath = entr;
    rooms_and_corridors.push_back(corridor_center2);

    std::vector<stockpile_type::type> types;
    types.push_back(stockpile_type::food);
    types.push_back(stockpile_type::furniture);
    types.push_back(stockpile_type::wood);
    types.push_back(stockpile_type::stone);
    types.push_back(stockpile_type::refuse);
    types.push_back(stockpile_type::animals);
    types.push_back(stockpile_type::corpses);
    types.push_back(stockpile_type::gems);
    types.push_back(stockpile_type::finished_goods);
    types.push_back(stockpile_type::cloth);
    types.push_back(stockpile_type::bars_blocks);
    types.push_back(stockpile_type::leather);
    types.push_back(stockpile_type::ammo);
    types.push_back(stockpile_type::armor);
    types.push_back(stockpile_type::weapons);
    types.push_back(stockpile_type::coins);
    auto type_it = types.begin();

    // TODO side stairs to workshop level ?
    for (int16_t dirx = -1; dirx <= 1; dirx += 2)
    {
        room *prev_corx = dirx < 0 ? corridor_center0 : corridor_center2;
        int16_t ocx = f.x + dirx * 3;
        for (int16_t dx = 1; dx <= 4; dx++)
        {
            // segments of the big central horizontal corridor
            int16_t cx = f.x + dirx * (8 * dx - 4);
            room *cor_x = new room(corridor_type::corridor, df::coord(ocx, f.y - 1, f.z), df::coord(cx + dirx, f.y + 1, f.z), stl_sprintf("%s stockpiles - segment %d", dirx == 1 ? "east" : "west", dx));
            cor_x->accesspath.push_back(prev_corx);
            rooms_and_corridors.push_back(cor_x);
            prev_corx = cor_x;
            ocx = cx + 2 * dirx;

            stockpile_type::type t0 = *type_it++;
            stockpile_type::type t1 = *type_it++;
            room *r0 = new room(t0, df::coord(cx - 3, f.y - 4, f.z), df::coord(cx + 3, f.y - 3, f.z));
            room *r1 = new room(t1, df::coord(cx - 3, f.y + 3, f.z), df::coord(cx + 3, f.y + 4, f.z));
            r0->level = 1;
            r1->level = 1;
            r0->accesspath.push_back(cor_x);
            r1->accesspath.push_back(cor_x);
            r0->layout.push_back(new_door(2, 2));
            r0->layout.push_back(new_door(4, 2));
            r1->layout.push_back(new_door(2, -1));
            r1->layout.push_back(new_door(4, -1));
            rooms_and_corridors.push_back(r0);
            rooms_and_corridors.push_back(r1);

            r0 = new room(t0, df::coord(cx - 3, f.y - 11, f.z), df::coord(cx + 3, f.y - 5, f.z));
            r1 = new room(t1, df::coord(cx - 3, f.y + 5, f.z), df::coord(cx + 3, f.y + 11, f.z));
            r0->level = 2;
            r1->level = 2;
            rooms_and_corridors.push_back(r0);
            rooms_and_corridors.push_back(r1);

            r0 = new room(t0, df::coord(cx - 3, f.y - 20, f.z), df::coord(cx + 3, f.y - 12, f.z));
            r1 = new room(t1, df::coord(cx - 3, f.y + 12, f.z), df::coord(cx + 3, f.y + 20, f.z));
            r0->level = 3;
            r1->level = 3;
            rooms_and_corridors.push_back(r0);
            rooms_and_corridors.push_back(r1);
        }
    }
    for (auto it = rooms_and_corridors.begin(); it != rooms_and_corridors.end(); it++)
    {
        room *r = *it;
        if (r->type == room_type::stockpile && r->stockpile_type == stockpile_type::coins && r->level > 1)
        {
            r->stockpile_type = stockpile_type::furniture;
            r->level += 2;
        }
    }

    return setup_blueprint_pitcage(out);
}

command_result Plan::setup_blueprint_pitcage(color_ostream &)
{
    room *gdump = find_room(room_type::garbagedump);
    if (!gdump)
        return CR_OK;
    auto layout = [](room *r)
    {
        r->layout.push_back(new_construction(construction_type::UpStair, -1, 1, -10));
        for (int16_t z = -9; z <= -1; z++)
        {
            r->layout.push_back(new_construction(construction_type::UpDownStair, -1, 1, z));
            for (int16_t x = -5; x <= 5; x++)
            {
                for (int16_t y = -5; y <= 5; y++)
                {
                    if (x == -1 && y == 1)
                    {
                        continue;
                    }
                    r->layout.push_back(new_dig(tile_dig_designation::Channel, x, y, z));
                }
            }
        }
        r->layout.push_back(new_construction(construction_type::DownStair, -1, 1, 0));
        std::vector<int16_t> dxs;
        dxs.push_back(0);
        dxs.push_back(1);
        dxs.push_back(2);
        std::vector<int16_t> dys;
        dys.push_back(1);
        dys.push_back(0);
        dys.push_back(2);
        for (auto dx = dxs.begin(); dx != dxs.end(); dx++)
        {
            for (auto dy = dys.begin(); dy != dys.end(); dy++)
            {
                if (*dx == 1 && *dy == 1)
                {
                    r->layout.push_back(new_dig(tile_dig_designation::Channel, *dx, *dy));
                }
                else
                {
                    r->layout.push_back(new_construction(construction_type::Floor, *dx, *dy));
                }
            }
        }
    };

    room *r = new room(room_type::pitcage, gdump->min + df::coord(-1, -1, 10), gdump->min + df::coord(1, 1, 10));
    layout(r);
    r->layout.push_back(new_hive_floor(3, 1));
    rooms_and_corridors.push_back(r);

    room *stockpile = new room(stockpile_type::animals, r->min, r->max, "pitting queue");
    stockpile->level = 0;
    stockpile->stock_specific1 = true; // no empty cages
    stockpile->stock_specific2 = true; // no empty animal traps
    layout(stockpile);
    rooms_and_corridors.push_back(stockpile);

    return CR_OK;
}

command_result Plan::setup_blueprint_utilities(color_ostream & out, df::coord f, const std::vector<room *> & entr)
{
    room *corridor_center0 = new room(corridor_type::corridor, f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0), "main staircase - west utilities");
    corridor_center0->layout.push_back(new_door(-1, 0));
    corridor_center0->layout.push_back(new_door(-1, 2));
    corridor_center0->accesspath = entr;
    rooms_and_corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(corridor_type::corridor, f + df::coord(1, -1, 0), f + df::coord(1, 1, 0), "main staircase - east utilities");
    corridor_center2->layout.push_back(new_door(1, 0));
    corridor_center2->layout.push_back(new_door(1, 2));
    corridor_center2->accesspath = entr;
    rooms_and_corridors.push_back(corridor_center2);

    // dining halls
    int16_t ocx = f.x - 2;
    room *old_cor = corridor_center0;

    // temporary dininghall, for fort start
    room *tmp = new room(room_type::dininghall, f + df::coord(-4, -1, 0), f + df::coord(-3, 1, 0), "temporary dining room");
    tmp->temporary = true;
    for (int16_t dy = 0; dy <= 2; dy++)
    {
        tmp->layout.push_back(new_furniture_with_users(layout_type::table, 0, dy, dwarves_per_table));
        tmp->layout.push_back(new_furniture_with_users(layout_type::chair, 1, dy, dwarves_per_table));
    }
    tmp->layout[0]->makeroom = true;
    tmp->accesspath.push_back(old_cor);
    rooms_and_corridors.push_back(tmp);

    // dininghalls x 4 (54 users each)
    for (int16_t ax = 0; ax <= 1; ax++)
    {
        room *cor = new room(corridor_type::corridor, df::coord(ocx - 1, f.y - 1, f.z), df::coord(f.x - ax * 12 - 5, f.y + 1, f.z), stl_sprintf("west utilities - segment %d", ax + 1));
        cor->accesspath.push_back(old_cor);
        rooms_and_corridors.push_back(cor);
        ocx = f.x - ax * 12 - 4;
        old_cor = cor;

        std::vector<int16_t> dxs;
        dxs.push_back(5);
        dxs.push_back(4);
        dxs.push_back(6);
        dxs.push_back(3);
        dxs.push_back(7);
        dxs.push_back(2);
        dxs.push_back(8);
        dxs.push_back(1);
        dxs.push_back(9);
        for (int16_t dy = -1; dy <= 1; dy += 2)
        {
            room *dinner = new room(room_type::dininghall, df::coord(f.x - ax * 12 - 2 - 10, f.y + dy * 9, f.z), df::coord(f.x - ax * 12 - 2, f.y + dy * 3, f.z), stl_sprintf("dining room %c", 'A' + ax + dy + 1));
            dinner->layout.push_back(new_door(7, dy > 0 ? -1 : 7));
            dinner->layout.push_back(new_wall(2, 3));
            dinner->layout.push_back(new_wall(8, 3));
            for (auto dx = dxs.begin(); dx != dxs.end(); dx++)
            {
                for (int16_t sy = -1; sy <= 1; sy += 2)
                {
                    dinner->layout.push_back(new_furniture_with_users(layout_type::table, *dx, 3 + dy * sy * 1, dwarves_per_table, true));
                    dinner->layout.push_back(new_furniture_with_users(layout_type::chair, *dx, 3 + dy * sy * 2, dwarves_per_table, true));
                }
            }
            for (auto f = dinner->layout.begin(); f != dinner->layout.end(); f++)
            {
                if ((*f)->type == layout_type::table)
                {
                    (*f)->makeroom = true;
                    break;
                }
            }
            dinner->accesspath.push_back(cor);
            rooms_and_corridors.push_back(dinner);
        }
    }

    // tavern
    room *cor = new room(corridor_type::corridor, f + df::coord(-18, -1, 0), f + df::coord(-26, 1, 0), "tavern entrance");
    cor->accesspath.push_back(old_cor);
    rooms_and_corridors.push_back(cor);

    df::coord tavern_center = f - df::coord(32, 0, 0);
    room *tavern = new room(location_type::tavern, tavern_center - df::coord(4, 4, 0), tavern_center + df::coord(4, 4, 0));
    tavern->layout.push_back(new_door(9, 3));
    tavern->layout.push_back(new_door(9, 5));
    tavern->layout.push_back(new_furniture(layout_type::chest, 8, 0));
    tavern->accesspath.push_back(cor);
    rooms_and_corridors.push_back(tavern);

    room *booze = new room(stockpile_type::food, tavern_center + df::coord(-2, -4, 0), tavern_center + df::coord(3, -4, 0), "tavern booze");
    booze->stock_disable.insert(stockpile_list::FoodMeat);
    booze->stock_disable.insert(stockpile_list::FoodFish);
    booze->stock_disable.insert(stockpile_list::FoodUnpreparedFish);
    booze->stock_disable.insert(stockpile_list::FoodEgg);
    booze->stock_disable.insert(stockpile_list::FoodPlants);
    booze->stock_disable.insert(stockpile_list::FoodCheesePlant);
    booze->stock_disable.insert(stockpile_list::FoodCheeseAnimal);
    booze->stock_disable.insert(stockpile_list::FoodSeeds);
    booze->stock_disable.insert(stockpile_list::FoodLeaves);
    booze->stock_disable.insert(stockpile_list::FoodMilledPlant);
    booze->stock_disable.insert(stockpile_list::FoodBoneMeal);
    booze->stock_disable.insert(stockpile_list::FoodFat);
    booze->stock_disable.insert(stockpile_list::FoodPaste);
    booze->stock_disable.insert(stockpile_list::FoodPressedMaterial);
    booze->stock_disable.insert(stockpile_list::FoodExtractPlant);
    booze->stock_disable.insert(stockpile_list::FoodExtractAnimal);
    booze->stock_disable.insert(stockpile_list::FoodMiscLiquid);
    booze->stock_specific1 = true; // no prepared food
    booze->workshop = tavern;
    booze->level = 0;
    tavern->accesspath.push_back(booze);
    rooms_and_corridors.push_back(booze);

    if (allow_ice)
    {
        ai->debug(out, "icy embark, no well");
        booze->min.x -= 2;
    }
    else
    {
        df::coord river = scan_river(out);
        if (river.isValid())
        {
            command_result res = setup_blueprint_cistern_fromsource(out, river, f, tavern);
            if (res != CR_OK)
                return res;
        }
        else
        {
            // TODO pool, pumps, etc
            ai->debug(out, "no river, no well");
            booze->min.x -= 2;
        }
    }

    // farm plots
    int16_t cx = f.x + 4 * 6; // end of workshop corridor (last ws door)
    int16_t cy = f.y;
    int16_t cz = find_room(room_type::workshop, [](room *r) -> bool { return r->workshop_type == workshop_type::Farmers; })->min.z;
    room *ws_cor = new room(corridor_type::corridor, df::coord(f.x + 3, cy, cz), df::coord(cx + 1, cy, cz), "farm access corridor"); // ws_corr->accesspath ...
    rooms_and_corridors.push_back(ws_cor);
    room *farm_stairs = new room(corridor_type::corridor, df::coord(cx + 2, cy, cz), df::coord(cx + 2, cy, cz), "farm stairs");
    farm_stairs->accesspath.push_back(ws_cor);
    rooms_and_corridors.push_back(farm_stairs);
    cx += 3;
    int16_t cz2 = cz;
    int32_t soilcnt = 0;
    for (int16_t z = cz; z < world->map.z_count; z++)
    {
        bool ok = true;
        int32_t scnt = 0;
        for (int16_t dx = -1; dx <= nrfarms * farm_w / 3; dx++)
        {
            for (int16_t dy = -3 * farm_h - farm_h + 1; dy <= 3 * farm_h + farm_h - 1; dy++)
            {
                df::tiletype *t = Maps::getTileType(cx + dx, cy + dy, z);
                if (!t || ENUM_ATTR(tiletype, shape, *t) != tiletype_shape::WALL)
                {
                    ok = false;
                    continue;
                }

                if (ENUM_ATTR(tiletype, material, *t) == tiletype_material::SOIL)
                {
                    scnt++;
                }
            }
        }

        if (ok && soilcnt < scnt)
        {
            soilcnt = scnt;
            cz2 = z;
        }
    }

    farm_stairs->max.z = cz2;
    cor = new room(corridor_type::corridor, df::coord(cx, cy, cz2), df::coord(cx + 1, cy, cz2), "farm corridor");
    cor->accesspath.push_back(farm_stairs);
    rooms_and_corridors.push_back(cor);
    room *first_farm = nullptr;
    room *prev_farm = nullptr;
    for (int16_t dx = 0; dx < nrfarms / 3; dx++)
    {
        for (int16_t ddy = 0; ddy < 3; ddy++)
        {
            for (int16_t dy = -1; dy <= 1; dy += 2)
            {
                room *r = new room(dy > 0 ? farm_type::cloth : farm_type::food, df::coord(cx + farm_w * dx, cy + dy * 2 + dy * ddy * farm_h, cz2), df::coord(cx + farm_w * dx + farm_w - 1, cy + dy * (2 + farm_h - 1) + dy * ddy * farm_h, cz2));
                r->has_users = dpf;
                if (dx == 0 && ddy == 0)
                {
                    r->layout.push_back(new_door(1, dy > 0 ? -1 : farm_h));
                    r->accesspath.push_back(cor);
                }
                else
                {
                    r->accesspath.push_back(prev_farm);
                }
                prev_farm = r;
                rooms_and_corridors.push_back(r);
                if (first_farm == nullptr)
                {
                    first_farm = r;
                }
            }
        }
    }

    // seeds stockpile
    room *r = new room(stockpile_type::food, df::coord(cx + 2, cy, cz2), df::coord(cx + 4, cy, cz2), "farm seeds stockpile");
    r->level = 0;
    r->stock_disable.insert(stockpile_list::FoodMeat);
    r->stock_disable.insert(stockpile_list::FoodFish);
    r->stock_disable.insert(stockpile_list::FoodUnpreparedFish);
    r->stock_disable.insert(stockpile_list::FoodEgg);
    r->stock_disable.insert(stockpile_list::FoodPlants);
    r->stock_disable.insert(stockpile_list::FoodDrinkPlant);
    r->stock_disable.insert(stockpile_list::FoodDrinkAnimal);
    r->stock_disable.insert(stockpile_list::FoodCheesePlant);
    r->stock_disable.insert(stockpile_list::FoodCheeseAnimal);
    r->stock_disable.insert(stockpile_list::FoodLeaves);
    r->stock_disable.insert(stockpile_list::FoodMilledPlant);
    r->stock_disable.insert(stockpile_list::FoodBoneMeal);
    r->stock_disable.insert(stockpile_list::FoodFat);
    r->stock_disable.insert(stockpile_list::FoodPaste);
    r->stock_disable.insert(stockpile_list::FoodPressedMaterial);
    r->stock_disable.insert(stockpile_list::FoodExtractPlant);
    r->stock_disable.insert(stockpile_list::FoodExtractAnimal);
    r->stock_disable.insert(stockpile_list::FoodMiscLiquid);
    r->stock_specific1 = true; // no prepared meals
    r->workshop = first_farm;
    r->accesspath.push_back(cor);
    rooms_and_corridors.push_back(r);

    // garbage dump
    // TODO ensure flat space, no pools/tree, ...
    df::coord tile(cx + 5, cy, cz);
    room *garbagedump = r = new room(room_type::garbagedump, tile, tile);
    tile = spiral_search(tile, [this, f, cx, cz](df::coord t) -> bool
    {
        t = surface_tile_at(t.x, t.y);
        if (!t.isValid())
            return false;
        if (t.x < cx + 5 && (t.z <= cz + 2 || t.x <= f.x + 5))
            return false;
        if (!map_tile_in_rock(t + df::coord(0, 0, -1)))
            return false;
        if (!map_tile_in_rock(t + df::coord(2, 0, -1)))
            return false;
        return !spiral_search(t + df::coord(1, 0, 0), 5, [](df::coord tt) -> bool
        {
            return ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(tt))) != tiletype_shape_basic::Floor;
        }).isValid();
    });
    tile = surface_tile_at(tile.x, tile.y);
    r->min = r->max = tile;
    garbagedump->layout.push_back(new_dig(tile_dig_designation::Channel, 0, 0));
    rooms_and_corridors.push_back(r);
    r = new room(corridor_type::corridor, tile + df::coord(-5, -5, 0), tile + df::coord(5, 5, 0));
    r->layout.push_back(new_dig(tile_dig_designation::Channel, 5, 5));
    r->layout.push_back(new_dig(tile_dig_designation::Channel, 6, 5));
    r->layout.push_back(new_construction(construction_type::Floor, 5, 5, -1));
    garbagedump->accesspath.push_back(r);
    rooms_and_corridors.push_back(r);

    // infirmary
    old_cor = corridor_center2;
    cor = new room(corridor_type::corridor, f + df::coord(3, -1, 0), f + df::coord(5, 1, 0), "east utilities - infirmary access");
    cor->accesspath.push_back(old_cor);
    rooms_and_corridors.push_back(cor);
    old_cor = cor;

    room *infirmary = new room(room_type::infirmary, f + df::coord(2, -3, 0), f + df::coord(6, -7, 0));
    infirmary->layout.push_back(new_door(3, 5));
    infirmary->layout.push_back(new_furniture(layout_type::bed, 0, 1));
    infirmary->layout.push_back(new_furniture(layout_type::table, 1, 1));
    infirmary->layout.push_back(new_furniture(layout_type::bed, 2, 1));
    infirmary->layout.push_back(new_furniture(layout_type::traction_bench, 0, 2));
    infirmary->layout.push_back(new_furniture(layout_type::traction_bench, 2, 2));
    infirmary->layout.push_back(new_furniture(layout_type::bed, 0, 3));
    infirmary->layout.push_back(new_furniture(layout_type::table, 1, 3));
    infirmary->layout.push_back(new_furniture(layout_type::bed, 2, 3));
    infirmary->layout.push_back(new_furniture(layout_type::chest, 4, 1));
    infirmary->layout.push_back(new_furniture(layout_type::chest, 4, 2));
    infirmary->layout.push_back(new_furniture(layout_type::chest, 4, 3));
    infirmary->accesspath.push_back(cor);
    rooms_and_corridors.push_back(infirmary);

    // cemetary lots (160 spots)
    cor = new room(corridor_type::corridor, f + df::coord(6, -1, 0), f + df::coord(14, 1, 0), "east utilities - cemetary access");
    cor->accesspath.push_back(old_cor);
    rooms_and_corridors.push_back(cor);
    old_cor = cor;

    for (int16_t ry = 0; ry < 500; ry++)
    {
        bool stop = false;
        for (int16_t tx = -1; !stop && tx <= 19; tx++)
        {
            for (int16_t ty = -1; !stop && ty <= 4; ty++)
            {
                df::tiletype *t = Maps::getTileType(f + df::coord(10 + tx, -3 - 3 * ry - ty, 0));
                if (!t || ENUM_ATTR(tiletype_shape, basic_shape,
                    ENUM_ATTR(tiletype, shape, *t)) !=
                    tiletype_shape_basic::Wall)
                {
                    stop = true;
                }
            }
        }
        if (stop)
            break;

        for (int16_t rrx = 0; rrx < 2; rrx++)
        {
            for (int16_t rx = 0; rx < 2; rx++)
            {
                df::coord o = f + df::coord(10 + 5 * rx + 9 * rrx, -3 - 3 * ry, 0);
                room *cemetary = new room(room_type::cemetary, o, o + df::coord(4, -3, 0));
                for (int16_t dx = 0; dx < 4; dx++)
                {
                    for (int16_t dy = 0; dy < 2; dy++)
                    {
                        cemetary->layout.push_back(new_furniture_with_users(layout_type::coffin, dx + 1 - rx, dy + 1, 1, true));
                    }
                }
                if (rx == 0 && ry == 0 && rrx == 0)
                {
                    cemetary->layout.push_back(new_door(4, 4));
                    cemetary->accesspath.push_back(cor);
                }
                rooms_and_corridors.push_back(cemetary);
            }
        }
    }

    // barracks
    // 8 dwarf per squad, 20% pop => 40 soldiers for 200 dwarves => 5 barracks
    char barracksLetter = 'A';
    old_cor = corridor_center2;
    int16_t oldcx = old_cor->max.x + 2; // door
    for (int16_t rx = 0; rx < 4; rx++)
    {
        cor = new room(corridor_type::corridor, df::coord(oldcx, f.y - 1, f.z), df::coord(f.x + 5 + 10 * rx, f.y + 1, f.z), stl_sprintf("east utilities - segment %d", rx + 1));
        cor->accesspath.push_back(old_cor);
        rooms_and_corridors.push_back(cor);
        old_cor = cor;
        oldcx = cor->max.x + 1;

        for (int16_t ry = -1; ry <= 1; ry += 2)
        {
            if (ry == -1 && rx < 3) // infirmary/cemetary
                continue;

            room *barracks = new room(room_type::barracks, df::coord(f.x + 2 + 10 * rx, f.y + 3 * ry, f.z), df::coord(f.x + 2 + 10 * rx + 6, f.y + 10 * ry, f.z), stl_sprintf("barracks %c", barracksLetter++));
            barracks->layout.push_back(new_door(3, ry > 0 ? -1 : 8));
            for (int16_t dy_ = 0; dy_ < 8; dy_++)
            {
                int16_t dy = ry < 0 ? 7 - dy_ : dy_;
                barracks->layout.push_back(new_furniture_with_users(layout_type::armor_stand, 5, dy, 1, true));
                barracks->layout.push_back(new_furniture_with_users(layout_type::bed, 6, dy, 1, true));
                barracks->layout.push_back(new_furniture_with_users(layout_type::cabinet, 0, dy, 1, true));
                barracks->layout.push_back(new_furniture_with_users(layout_type::chest, 1, dy, 1, true));
            }
            barracks->layout.push_back(new_furniture_with_users(layout_type::weapon_rack, 4, ry > 0 ? 7 : 0, 5, false));
            barracks->layout.back()->makeroom = true;
            barracks->layout.push_back(new_furniture_with_users(layout_type::weapon_rack, 2, ry > 0 ? 7 : 0, 5, true));
            barracks->layout.push_back(new_furniture_with_users(layout_type::archery_target, 3, ry > 0 ? 7 : 0, 10, true));
            barracks->accesspath.push_back(cor);
            rooms_and_corridors.push_back(barracks);
        }
    }

    ai->debug(out, "finished interior utilities");
    command_result res;
    res = setup_blueprint_pastures(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "finished pastures");
    res = setup_blueprint_outdoor_farms(out, nrfarms * 2);
    if (res != CR_OK)
        return res;
    ai->debug(out, "finished outdoor farms");
    return CR_OK;
}

command_result Plan::setup_blueprint_cistern_fromsource(color_ostream & out, df::coord src, df::coord f, room *tavern)
{
    // TODO dynamic layout, at least move the well/cistern on the side of the river
    // TODO scan for solid ground for all this

    // add a well to the tavern
    tavern->layout.push_back(new_well(4, 4));
    tavern->layout.push_back(new_cistern_lever(1, 0, "out", false));
    tavern->layout.push_back(new_cistern_lever(0, 0, "in", true));

    df::coord c = tavern->pos();

    // water cistern under the well (in the hole of bedroom blueprint)
    std::vector<room *> cist_cors = find_corridor_tosurface(out, corridor_type::aqueduct, c - df::coord(8, 0, 0));
    cist_cors.at(0)->min.z -= 3;

    room *cistern = new room(cistern_type::well, c + df::coord(-7, -1, -3), c + df::coord(1, 1, -1));
    cistern->accesspath.push_back(cist_cors.at(0));

    // handle low rivers / high mountain forts
    if (f.z > src.z)
        f.z = c.z = src.z;
    // should be fine with cistern auto-fill checks
    if (cistern->min.z > f.z)
        cistern->min.z = f.z;

    // staging reservoir to fill the cistern, one z-level at a time
    // should have capacity = 1 cistern level @7/7 + itself @1/7 (rounded up)
    //  cistern is 9x3 + 1 (stairs)
    //  reserve is 5x7 (can fill cistern 7/7 + itself 1/7 + 14 spare
    room *reserve = new room(cistern_type::reserve, c + df::coord(-10, -3, 0), c + df::coord(-14, 3, 0));
    reserve->layout.push_back(new_cistern_floodgate(-1, 3, "in", true, false));
    reserve->layout.push_back(new_cistern_floodgate(5, 3, "out", false, true));
    reserve->accesspath.push_back(cist_cors.at(0));

    // cisterns are dug in order
    // try to dig reserve first, so we have liquid water even if river freezes
    rooms_and_corridors.push_back(reserve);
    rooms_and_corridors.push_back(cistern);

    // link the cistern reserve to the water source

    // trivial walk of the river tiles to find a spot closer to dst
    auto move_river = [this, &src](df::coord dst)
    {
        auto distance = [](df::coord a, df::coord b) -> int16_t
        {
            df::coord d = a - b;
            return d.x * d.x + d.y * d.y + d.z * d.z;
        };

        df::coord nsrc = src;
        for (size_t i = 0; i < 500; i++)
        {
            if (!nsrc.isValid())
                break;
            src = nsrc;
            int16_t dist = distance(src, dst);
            nsrc = spiral_search(src, 1, 1, [distance, dist, dst](df::coord t) -> bool
            {
                if (distance(t, dst) > dist)
                    return false;
                return Maps::getTileDesignation(t)->bits.feature_local;
            });
        }
    };

    // 1st end: reservoir input
    df::coord p1 = c - df::coord(16, 0, 0);
    move_river(p1);
    ai->debug(out, stl_sprintf("cistern: reserve/in (%d, %d, %d), river (%d, %d, %d)", p1.x, p1.y, p1.z, src.x, src.y, src.z));

    df::coord p = p1;
    room *r = reserve;
    // XXX hardcoded layout again
    if (src.x > p1.x)
    {
        // the tunnel should walk around other blueprint rooms
        df::coord p2 = p1 + df::coord(0, src.y >= p1.y ? 26 : -26, 0);
        room *cor = new room(corridor_type::aqueduct, p1, p2);
        rooms_and_corridors.push_back(cor);
        reserve->accesspath.push_back(cor);
        move_river(p2);
        p = p2;
        r = cor;
    }

    std::vector<room *> up = find_corridor_tosurface(out, corridor_type::aqueduct, p);
    r->accesspath.push_back(up.at(0));

    df::coord dst = up.back()->max - df::coord(0, 0, 2);
    if (src.z <= dst.z)
        dst.z = src.z - 1;
    move_river(dst);

    if (std::abs(dst.x - src.x) > 1)
    {
        df::coord p3(src.x, dst.y, dst.z);
        move_river(p3);
    }

    // find safe tile near the river and a tile to channel
    df::coord channel;
    channel.clear();
    df::coord output = spiral_search(src, [this, &channel](df::coord t) -> bool
    {
        if (!map_tile_in_rock(t))
        {
            return false;
        }
        channel = Plan::spiral_search(t, 1, 1, [](df::coord t) -> bool
        {
            return Plan::spiral_search(t, 1, 1, [](df::coord t) -> bool
            {
                return Maps::getTileDesignation(t)->bits.feature_local;
            }).isValid();
        });
        if (!channel.isValid())
        {
            channel = Plan::spiral_search(t, 1, 1, [](df::coord t) -> bool
            {
                return Plan::spiral_search(t, 1, 1, [](df::coord t) -> bool
                {
                    return Maps::getTileDesignation(t)->bits.flow_size != 0 ||
                        ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::FROZEN_LIQUID;
                }).isValid();
            });
        }
        return channel.isValid();
    });

    if (channel.isValid())
    {
        ai->debug(out, stl_sprintf("cistern: out(%d, %d, %d), channel_enable (%d, %d, %d)", output.x, output.y, output.z, channel.x, channel.y, channel.z));
    }

    // TODO check that 'channel' is easily channelable (eg river in a hole)

    if (dst.x != output.x)
    {
        room *cor = new room(corridor_type::aqueduct, df::coord(dst.x, dst.y, dst.z), df::coord(output.x, dst.y, dst.z));
        rooms_and_corridors.push_back(cor);
        r->accesspath.push_back(cor);
        r = cor;
    }

    if (dst.y != output.y)
    {
        room *cor = new room(corridor_type::aqueduct, df::coord(output.x, dst.y, dst.z), df::coord(output.x, output.y, dst.z));
        rooms_and_corridors.push_back(cor);
        r->accesspath.push_back(cor);
        r = cor;
    }

    up = find_corridor_tosurface(out, corridor_type::aqueduct, df::coord(output, dst.z));
    r->accesspath.push_back(up.at(0));

    reserve->channel_enable = channel;
    return CR_OK;
}

// scan for 11x11 flat areas with grass
command_result Plan::setup_blueprint_pastures(color_ostream & out)
{
    size_t want = 36;
    spiral_search(fort_entrance->pos(), std::max(world->map.x_count, world->map.y_count), 10, 5, [this, &out, &want](df::coord _t) -> bool
    {
        df::coord sf = surface_tile_at(_t.x, _t.y);
        if (!sf.isValid())
            return false;
        size_t floortile = 0;
        size_t grasstile = 0;
        bool ok = true;
        for (int16_t dx = -5; ok && dx <= 5; dx++)
        {
            for (int16_t dy = -5; ok && dy <= 5; dy++)
            {
                df::coord t = sf + df::coord(dx, dy, 0);
                df::tiletype *tt = Maps::getTileType(t);
                if (!tt)
                {
                    ok = false;
                    continue;
                }
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Floor && ENUM_ATTR(tiletype, material, *tt) != tiletype_material::TREE)
                {
                    ok = false;
                    continue;
                }
                floortile++;
                auto & events = Maps::getTileBlock(t)->block_events;
                for (auto be = events.begin(); be != events.end(); be++)
                {
                    df::block_square_event_grassst *grass = virtual_cast<df::block_square_event_grassst>(*be);
                    if (grass && grass->amount[t.x & 0xf][t.y & 0xf] > 0)
                    {
                        grasstile++;
                        break;
                    }
                }
            }
        }
        if (ok && floortile >= 9 * 9 && grasstile >= 8 * 8)
        {
            room *r = new room(room_type::pasture, sf - df::coord(5, 5, 0), sf + df::coord(5, 5, 0));
            r->has_users = 10000;
            rooms_and_corridors.push_back(r);
            want--;
        }
        return want == 0;
    });
    return CR_OK;
}

// scan for 3x3 flat areas with soil
command_result Plan::setup_blueprint_outdoor_farms(color_ostream & out, size_t want)
{
    spiral_search(fort_entrance->pos(), std::max(world->map.x_count, world->map.y_count), 9, 3, [this, &out, &want](df::coord _t) -> bool
    {
        df::coord sf = surface_tile_at(_t.x, _t.y, true);
        if (!sf.isValid())
            return false;
        df::tile_designation sd = *Maps::getTileDesignation(sf);
        for (int16_t dx = -1; dx <= 1; dx++)
        {
            for (int16_t dy = -1; dy <= 1; dy++)
            {
                df::coord t = sf + df::coord(dx, dy, 0);
                df::tile_designation *td = Maps::getTileDesignation(t);
                if (!td)
                {
                    return false;
                }
                if (sd.bits.subterranean != td->bits.subterranean)
                {
                    return false;
                }
                if (!sd.bits.subterranean &&
                    sd.bits.biome != td->bits.biome)
                {
                    return false;
                }
                df::tiletype tt = *Maps::getTileType(t);
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Floor)
                {
                    return false;
                }
                if (td->bits.flow_size != 0)
                {
                    return false;
                }
                if (!farm_allowed_materials.set.count(ENUM_ATTR(tiletype, material, tt)))
                {
                    return false;
                }
            }
        }
        room *r = new room(want % 2 == 0 ? farm_type::food : farm_type::cloth, sf - df::coord(1, 1, 0), sf + df::coord(1, 1, 0));
        r->has_users = dpf;
        r->outdoor = true;
        rooms_and_corridors.push_back(r);
        want--;
        return want == 0;
    });
    return CR_OK;
}

command_result Plan::setup_blueprint_bedrooms(color_ostream &, df::coord f, const std::vector<room *> & entr, int level)
{
    room *corridor_center0 = new room(corridor_type::corridor, f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0), stl_sprintf("main staircase - west %s bedrooms", level == 0 ? "upper" : "lower"));
    corridor_center0->layout.push_back(new_door(-1, 0));
    corridor_center0->layout.push_back(new_door(-1, 2));
    corridor_center0->accesspath = entr;
    rooms_and_corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(corridor_type::corridor, f + df::coord(1, -1, 0), f + df::coord(1, 1, 0), stl_sprintf("main staircase - east %s bedrooms", level == 0 ? "upper" : "lower"));
    corridor_center2->layout.push_back(new_door(1, 0));
    corridor_center2->layout.push_back(new_door(1, 2));
    corridor_center2->accesspath = entr;
    rooms_and_corridors.push_back(corridor_center2);

    for (int16_t dirx = -1; dirx <= 1; dirx += 2)
    {
        room *prev_corx = dirx < 0 ? corridor_center0 : corridor_center2;
        int16_t ocx = f.x + dirx * 3;
        for (int16_t dx = 1; dx <= 3; dx++)
        {
            // segments of the big central horizontal corridor
            int16_t cx = f.x + dirx * (9 * dx - 4);
            room *cor_x = new room(corridor_type::corridor, df::coord(ocx, f.y - 1, f.z), df::coord(cx, f.y + 1, f.z), stl_sprintf("%s %s bedrooms - segment %d", dirx == -1 ? "west" : "east", level == 0 ? "upper" : "lower", dx));
            cor_x->accesspath.push_back(prev_corx);
            rooms_and_corridors.push_back(cor_x);
            prev_corx = cor_x;
            ocx = cx + dirx;

            for (int16_t diry = -1; diry <= 1; diry += 2)
            {
                room *prev_cory = cor_x;
                int16_t ocy = f.y + diry * 2;
                for (int16_t dy = 1; dy <= 6; dy++)
                {
                    int16_t cy = f.y + diry * 3 * dy;
                    room *cor_y = new room(corridor_type::corridor, df::coord(cx, ocy, f.z), df::coord(cx - dirx, cy, f.z), stl_sprintf("%s %s %s bedrooms - segment %d-%d", diry == -1 ? "north" : "south", dirx == -1 ? "west" : "east", level == 0 ? "upper" : "lower", dx, dy));
                    cor_y->accesspath.push_back(prev_cory);
                    rooms_and_corridors.push_back(cor_y);
                    prev_cory = cor_y;
                    ocy = cy + diry;

                    auto bedroom = [this, cx, diry, cor_y](room *r)
                    {
                        r->accesspath.push_back(cor_y);
                        r->layout.push_back(new_furniture(layout_type::bed, r->min.x < cx ? 0 : 1, diry < 0 ? 1 : 0));
                        r->layout.back()->makeroom = true;
                        r->layout.push_back(new_furniture(layout_type::cabinet, r->min.x < cx ? 0 : 1, diry < 0 ? 0 : 1));
                        r->layout.back()->ignore = true;
                        r->layout.push_back(new_furniture(layout_type::chest, r->min.x < cx ? 1 : 0, diry < 0 ? 0 : 1));
                        r->layout.back()->ignore = true;
                        r->layout.push_back(new_door(r->min.x < cx ? 2 : -1, diry < 0 ? 1 : 0));
                        rooms_and_corridors.push_back(r);
                    };
                    bedroom(new room(room_type::bedroom, df::coord(cx - dirx * 4, cy, f.z), df::coord(cx - dirx * 3, cy + diry, f.z), stl_sprintf("%s %s %s bedrooms - room %d-%d A", diry == -1 ? "north" : "south", dirx == -1 ? "west" : "east", level == 0 ? "upper" : "lower", dx, dy)));
                    bedroom(new room(room_type::bedroom, df::coord(cx + dirx * 2, cy, f.z), df::coord(cx + dirx * 3, cy + diry, f.z), stl_sprintf("%s %s %s bedrooms - room %d-%d B", diry == -1 ? "north" : "south", dirx == -1 ? "west" : "east", level == 0 ? "upper" : "lower", dx, dy)));
                }
            }
        }

        // noble suites
        int16_t cx = f.x + dirx * (9 * 3 - 4 + 6);
        room *cor_x = new room(corridor_type::corridor, df::coord(ocx, f.y - 1, f.z), df::coord(cx, f.y + 1, f.z), stl_sprintf("%s %s bedrooms - noble room hallway", dirx == -1 ? "west" : "east", level == 0 ? "upper" : "lower"));
        room *cor_x2 = new room(corridor_type::corridor, df::coord(ocx - dirx, f.y, f.z), df::coord(f.x + dirx * 3, f.y, f.z));
        cor_x->accesspath.push_back(cor_x2);
        cor_x2->accesspath.push_back(dirx < 0 ? corridor_center0 : corridor_center2);
        rooms_and_corridors.push_back(cor_x);
        rooms_and_corridors.push_back(cor_x2);

        for (int16_t diry = -1; diry <= 1; diry += 2)
        {
            noblesuite++;

            room *r = new room(nobleroom_type::office, df::coord(cx - 1, f.y + diry * 3, f.z), df::coord(cx + 1, f.y + diry * 5, f.z));
            r->noblesuite = noblesuite;
            r->accesspath.push_back(cor_x);
            r->layout.push_back(new_furniture(layout_type::chair, 1, 1));
            r->layout.back()->makeroom = true;
            r->layout.push_back(new_furniture(layout_type::table, 1 - dirx, 1));
            r->layout.push_back(new_furniture(layout_type::chest, 1 + dirx, 0));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_furniture(layout_type::cabinet, 1 + dirx, 2));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_door(1, 1 - 2 * diry));
            rooms_and_corridors.push_back(r);
            room *prev_room = r;

            r = new room(nobleroom_type::bedroom, df::coord(cx - 1, f.y + diry * 7, f.z), df::coord(cx + 1, f.y + diry * 9, f.z));
            r->noblesuite = noblesuite;
            r->layout.push_back(new_furniture(layout_type::bed, 1, 1));
            r->layout.back()->makeroom = true;
            r->layout.push_back(new_furniture(layout_type::armor_stand, 1 - dirx, 0));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_furniture(layout_type::weapon_rack, 1 - dirx, 2));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_door(1, 1 - 2 * diry, true));
            r->accesspath.push_back(prev_room);
            rooms_and_corridors.push_back(r);
            prev_room = r;

            r = new room(nobleroom_type::dining, df::coord(cx - 1, f.y + diry * 11, f.z), df::coord(cx + 1, f.y + diry * 13, f.z));
            r->noblesuite = noblesuite;
            r->layout.push_back(new_furniture(layout_type::table, 1, 1));
            r->layout.back()->makeroom = true;
            r->layout.push_back(new_furniture(layout_type::chair, 1 + dirx, 1));
            r->layout.push_back(new_furniture(layout_type::cabinet, 1 - dirx, 0));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_furniture(layout_type::chest, 1 - dirx, 2));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_door(1, 1 - 2 * diry, true));
            r->accesspath.push_back(prev_room);
            rooms_and_corridors.push_back(r);
            prev_room = r;

            r = new room(nobleroom_type::tomb, df::coord(cx - 1, f.y + diry * 15, f.z), df::coord(cx + 1, f.y + diry * 17, f.z));
            r->noblesuite = noblesuite;
            r->layout.push_back(new_furniture(layout_type::coffin, 1, 1));
            r->layout.back()->makeroom = true;
            r->layout.push_back(new_door(1, 1 - 2 * diry, true));
            r->accesspath.push_back(prev_room);
            rooms_and_corridors.push_back(r);
        }
    }
    return CR_OK;
}

command_result Plan::setup_blueprint_stockpiles_overflow(color_ostream &, df::coord f, const std::vector<room *> & entr)
{
    room *access_west = new room(corridor_type::corridor, f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0), "main staircase - west stockpile overflow");
    access_west->layout.push_back(new_door(-1, 0));
    access_west->layout.push_back(new_door(-1, 2));
    access_west->accesspath = entr;
    rooms_and_corridors.push_back(access_west);

    room *access_east = new room(corridor_type::corridor, f + df::coord(1, -1, 0), f + df::coord(1, 1, 0), "main staircase - east stockpile overflow");
    access_east->layout.push_back(new_door(1, 0));
    access_east->layout.push_back(new_door(1, 2));
    access_east->accesspath = entr;
    rooms_and_corridors.push_back(access_east);

    for (int32_t dx = 0; dx < 4; dx++)
    {
        room *r = new room(stockpile_type::food, f + df::coord(-dx * 10 - 12, -2, 0), f + df::coord(-dx * 10 - 3, 2, 0), stl_sprintf("stockpile overflow - west %d", dx + 1));
        r->level = 4 + dx * 3;
        r->accesspath.push_back(access_west);
        rooms_and_corridors.push_back(r);
        access_west = r;
        room *access_northwest = access_west;
        room *access_southwest = access_west;

        r = new room(stockpile_type::food, f + df::coord(dx * 10 + 3, -2, 0), f + df::coord(dx * 10 + 12, 2, 0), stl_sprintf("stockpile overflow - east %d", dx + 1));
        r->level = 4 + dx * 3;
        r->accesspath.push_back(access_east);
        rooms_and_corridors.push_back(r);
        access_east = r;
        room *access_northeast = access_east;
        room *access_southeast = access_east;

        for (int32_t dy = 0; dy < 2; dy++)
        {
            r = new room(stockpile_type::food, f + df::coord(-dx * 10 - 12, -dy * 10 - 12, 0), f + df::coord(-dx * 10 - 3, -dy * 10 - 3, 0), stl_sprintf("stockpile overflow - north %d west %d", dy + 1, dx + 1));
            r->level = 4 + dx * 3 + 1 + dy;
            r->accesspath.push_back(access_northwest);
            rooms_and_corridors.push_back(r);
            access_northwest = r;

            r = new room(stockpile_type::food, f + df::coord(-dx * 10 - 12, dy * 10 + 3, 0), f + df::coord(-dx * 10 - 3, dy * 10 + 12, 0), stl_sprintf("stockpile overflow - south %d west %d", dy + 1, dx + 1));
            r->level = 4 + dx * 3 + 1 + dy;
            r->accesspath.push_back(access_southwest);
            rooms_and_corridors.push_back(r);
            access_southwest = r;

            r = new room(stockpile_type::food, f + df::coord(dx * 10 + 3, -dy * 10 - 12, 0), f + df::coord(dx * 10 + 12, -dy * 10 - 3, 0), stl_sprintf("stockpile overflow - north %d east %d", dy + 1, dx + 1));
            r->level = 4 + dx * 3 + 1 + dy;
            r->accesspath.push_back(access_northeast);
            rooms_and_corridors.push_back(r);
            access_northeast = r;

            r = new room(stockpile_type::food, f + df::coord(dx * 10 + 3, dy * 10 + 3, 0), f + df::coord(dx * 10 + 12, dy * 10 + 12, 0), stl_sprintf("stockpile overflow - south %d east %d", dy + 1, dx + 1));
            r->level = 4 + dx * 3 + 1 + dy;
            r->accesspath.push_back(access_southeast);
            rooms_and_corridors.push_back(r);
            access_southeast = r;

            if (dx == 0)
            {
                r = new room(stockpile_type::food, f + df::coord(-2, -dy * 10 - 12, 0), f + df::coord(2, -dy * 10 - 3, 0), stl_sprintf("stockpile overflow - north %d", dy + 1));
                r->level = 4 + 2 + dy;
                r->accesspath.push_back(access_northwest);
                r->accesspath.push_back(access_northeast);
                rooms_and_corridors.push_back(r);

                r = new room(stockpile_type::food, f + df::coord(-2, dy * 10 + 3, 0), f + df::coord(2, dy * 10 + 12, 0), stl_sprintf("stockpile overflow - south %d", dy + 1));
                r->level = 4 + 2 + dy;
                r->accesspath.push_back(access_southwest);
                r->accesspath.push_back(access_southeast);
                rooms_and_corridors.push_back(r);
            }
        }
    }

    return CR_OK;
}

void Plan::checkidle_legacy(color_ostream & out, std::ostream & reason)
{
    // if nothing better to do, order the miners to dig remaining
    // stockpiles, workshops, and a few bedrooms
    auto ifplan = [](room *r) -> bool
    {
        return r->status == room_status::plan;
    };
    int32_t freebed = spare_bedroom;
    room *r = nullptr;
#define FIND_ROOM(cond, type, lambda) \
    if (r == nullptr && (cond)) \
        r = find_room(type, lambda)

    FIND_ROOM(true, room_type::stockpile, [](room *r) -> bool
    {
        return r->stockpile_type == stockpile_type::food &&
            r->level == 0 &&
            r->workshop == nullptr &&
            r->status == room_status::plan;
    });
    FIND_ROOM(!important_workshops.empty(), room_type::workshop, [this](room *r) -> bool
    {
        if (r->workshop_type == important_workshops.back() &&
            r->status == room_status::plan &&
            r->level == 0)
        {
            important_workshops.pop_back();
            return true;
        }
        return false;
    });
    FIND_ROOM(true, room_type::tradedepot, ifplan);
    FIND_ROOM(true, room_type::cistern, ifplan);
    FIND_ROOM(true, room_type::infirmary, ifplan);
    FIND_ROOM(!find_room(room_type::cemetary, [](room *r) -> bool { return r->status != room_status::plan; }), room_type::cemetary, ifplan);
    FIND_ROOM(!important_workshops2.empty(), room_type::furnace, [this](room *r) -> bool
    {
        if (r->furnace_type == important_workshops2.back() &&
            r->status == room_status::plan &&
            r->level == 0)
        {
            important_workshops2.pop_back();
            return true;
        }
        return false;
    });
    FIND_ROOM(!important_workshops3.empty(), room_type::workshop, [this](room *r) -> bool
    {
        if (r->workshop_type == important_workshops3.back() &&
            r->status == room_status::plan &&
            r->level == 0)
        {
            important_workshops3.pop_back();
            return true;
        }
        return false;
    });
    if (r == nullptr)
        should_search_for_metal = true;
    FIND_ROOM(true, room_type::pitcage, ifplan);
    FIND_ROOM(true, room_type::stockpile, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level <= 1 &&
            r->workshop == nullptr;
    });
    FIND_ROOM(true, room_type::workshop, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level == 0;
    });
    FIND_ROOM(true, room_type::furnace, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level == 0;
    });
    if (r == nullptr && !fort_entrance->furnished)
    {
        r = fort_entrance;
        for (auto wagon : world->buildings.other[buildings_other_id::WAGON])
        {
            Buildings::deconstruct(wagon);
        }
        deconstructed_wagons = true;
    }
    int32_t need_food = extra_farms;
    int32_t need_cloth = extra_farms;
    FIND_ROOM(true, room_type::farmplot, ([&need_food, &need_cloth](room *r) -> bool
    {
        if (!r->users.empty())
        {
            return false;
        }

        if (r->farm_type == farm_type::food)
        {
            if (need_food <= 0)
            {
                return false;
            }

            if (r->status == room_status::plan)
            {
                return true;
            }

            need_food--;
        }
        else if (r->farm_type == farm_type::cloth)
        {
            if (need_cloth <= 0)
            {
                return false;
            }

            if (r->status == room_status::plan)
            {
                return true;
            }

            need_cloth--;
        }
        return false;
    }));
    FIND_ROOM(true, room_type::location, [](room *r) -> bool { return r->status == room_status::plan && r->location_type == location_type::tavern; });
    FIND_ROOM(true, room_type::outpost, [](room *r) -> bool
    {
        return r->status == room_status::plan && r->outpost_type == outpost_type::cavern;
    });
    FIND_ROOM(true, room_type::location, ifplan);
    FIND_ROOM(true, room_type::workshop, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level == 1;
    });
    FIND_ROOM(true, room_type::furnace, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level == 1;
    });
    FIND_ROOM(true, room_type::bedroom, [&freebed](room *r) -> bool
    {
        if (r->owner == -1)
        {
            freebed--;
            return freebed >= 0 && r->status == room_status::plan;
        }
        return false;
    });
    FIND_ROOM(true, room_type::stockpile, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level <= 2 &&
            r->workshop == nullptr;
    });
    auto finished_nofurnished = [](room *r) -> bool
    {
        return r->status == room_status::finished && !r->furnished;
    };
    FIND_ROOM(true, room_type::location, finished_nofurnished);
    FIND_ROOM(true, room_type::nobleroom, finished_nofurnished);
    FIND_ROOM(true, room_type::bedroom, finished_nofurnished);
    auto nousers_noplan = [](room *r) -> bool
    {
        return r->status != room_status::plan && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool
        {
            return f->has_users && f->users.empty();
        }) != r->layout.end();
    };
    auto nousers_plan = [](room *r) -> bool
    {
        return r->status == room_status::plan && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool
        {
            return f->has_users && f->users.empty();
        }) != r->layout.end();
    };
    FIND_ROOM(!find_room(room_type::dininghall, nousers_noplan), room_type::dininghall, nousers_plan);
    FIND_ROOM(!find_room(room_type::barracks, nousers_noplan), room_type::barracks, nousers_plan);
    FIND_ROOM(true, room_type::stockpile, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level <= 3 &&
            r->workshop == nullptr;
    });
    FIND_ROOM(true, room_type::workshop, ifplan);
    FIND_ROOM(true, room_type::furnace, ifplan);
    FIND_ROOM(true, room_type::farmplot, ifplan);
    FIND_ROOM(true, room_type::stockpile, ifplan);
    FIND_ROOM(true, room_type::outpost, ifplan);
    if (r == nullptr)
    {
        if (!past_initial_phase)
        {
            past_initial_phase = true;
            std::function<bool(room *)> unignore_all_furniture = [this, &out](room *r) -> bool
            {
                for (auto f : r->layout)
                {
                    f->ignore = false;
                }
                if (r->status == room_status::dug || r->status == room_status::finished)
                {
                    furnish_room(out, r);
                }
                return false;
            };
            find_room(room_type::dininghall, unignore_all_furniture);
            find_room(room_type::barracks, unignore_all_furniture);
            find_room(room_type::nobleroom, unignore_all_furniture);
            find_room(room_type::bedroom, unignore_all_furniture);
            find_room(room_type::cemetary, unignore_all_furniture);
        }

        bool any_outpost = false;
        for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
        {
            if ((*it)->type != task_type::want_dig && (*it)->type != task_type::dig_room)
            {
                continue;
            }
            if ((*it)->r->type == room_type::outpost || ((*it)->r->type == room_type::corridor && (*it)->r->corridor_type == corridor_type::outpost))
            {
                any_outpost = true;
                break;
            }
        }
        if (!any_outpost && setup_blueprint_caverns(out) == CR_OK)
        {
            ai->debug(out, "found next cavern");
            categorize_all();
            reason << "digging next outpost";
            return;
        }
    }
    FIND_ROOM(true, room_type::dininghall, ifplan);
    FIND_ROOM(true, room_type::barracks, ifplan);
    FIND_ROOM(true, room_type::nobleroom, ifplan);
    FIND_ROOM(true, room_type::bedroom, ifplan);
    FIND_ROOM(true, room_type::cemetary, ifplan);
#undef FIND_ROOM

    if (r)
    {
        ai->debug(out, "checkidle " + describe_room(r));
        reason << "queued room: " << describe_room(r);
        wantdig(out, r);
        if (r->status == room_status::finished)
        {
            r->furnished = true;
            for (auto it = r->layout.begin(); it != r->layout.end(); it++)
            {
                (*it)->ignore = false;
            }
            reason << " (for finishing)";
            furnish_room(out, r);
            smooth_room(out, r);
        }
    }
}
