#include "ai.h"
#include "plan_setup.h"
#include "plan.h"
#include "blueprint.h"
#include "debug.h"

#include "df/inorganic_raw.h"

class plan_setup_screen_helper
{
public:
    plan_setup_screen_helper(PlanSetup & setup)
    {
        Screen::show(std::make_unique<viewscreen_ai_plan_setupst>(setup), plugin_self);
    }
    ~plan_setup_screen_helper()
    {
        if (auto screen = Gui::getViewscreenByType<viewscreen_ai_plan_setupst>())
        {
            Screen::dismiss(screen);
        }
    }
};

PlanSetup::PlanSetup(AI & ai) :
    ExclusiveCallback("blueprint setup", 2),
    ai(ai),
    next_noblesuite(0)
{
}

PlanSetup::~PlanSetup()
{
    clear();
}

void PlanSetup::Run(color_ostream & out)
{
    plan_setup_screen_helper screen_helper(*this);
    ExpectScreen<viewscreen_ai_plan_setupst>("dfhack/df-ai/plan/setup");

    Log("Reading blueprints...");
    blueprints_t blueprints(out);

    if (build_from_blueprint(blueprints))
    {
        create_from_blueprint(ai.plan.fort_entrance, ai.plan.rooms_and_corridors, ai.plan.priorities);

        ai.plan.categorize_all();

        if (ai.plan.priorities.empty())
        {
            Log("WARNING: No priorities defined!");
        }
    }
    else
    {
        Log("Failed to build blueprint.");
        AI::abandon(out);
        return;
    }

    Log("Creating outdoor gathering zones...");
    ai.plan.setup_outdoor_gathering_zones(out);

    Log("Searching for caverns...");
    auto res = ai.plan.setup_blueprint_caverns(out);
    if (res == CR_OK)
        LogQuiet("Marked outpost location.");
    else
        LogQuiet("Could not find a cavern.");

    Log("Searching for mineral veins...");
    ai.plan.list_map_veins(out);

    for (auto & vein : ai.plan.map_veins)
    {
        auto ore = df::inorganic_raw::find(vein.first);
        if (ore->flags.is_set(inorganic_flags::METAL_ORE))
        {
            LogQuiet("Planning tunnel to " + ore->id + " vein...");
            ai.plan.dig_vein(out, vein.first, 0);
        }
    }

    Log("Finding areas that need access tunnels...");
    ai.plan.make_map_walkable(out);

    Log("Planning complete!");
    ai.plan.categorize_all();
}

void PlanSetup::Log(const std::string & message)
{
    ai.debug(Core::getInstance().getConsole(), "[Setup] " + message);
    log.push_back(std::make_pair(message, true));
    Delay();
}

void PlanSetup::LogQuiet(const std::string & message)
{
    int count = 5;
    for (auto it = log.rbegin(); it != log.rend(); it++)
    {
        if (it->second)
        {
            break;
        }
        count--;

        if (count <= 0)
        {
            log.erase(std::next(it).base());
            break;
        }
    }
    log.push_back(std::make_pair(message, false));
    Delay();
}

bool PlanSetup::build_from_blueprint(const blueprints_t & blueprints)
{
    std::vector<const blueprint_plan_template *> templates;
    for (auto & bp : blueprints.plans)
    {
        templates.push_back(bp.second);
    }

    std::shuffle(templates.begin(), templates.end(), ai.rng);

    Log(stl_sprintf("There are %zu blueprint plans.", templates.size()));

    for (size_t i = 0; i < templates.size(); i++)
    {
        const blueprint_plan_template & plan = *templates.at(i);
        for (size_t retries = 0; retries < plan.max_retries; retries++)
        {
            DFAI_DEBUG(blueprint, 1, "Trying to create a blueprint using plan " << (i + 1) << " of " << templates.size() << ": " << plan.name << " (attempt " << (retries + 1) << " of " << plan.max_retries << ")");

            Log(stl_sprintf("Trying plan %zu: %s (attempt %zu of %zu)", i + 1, plan.name.c_str(), retries + 1, plan.max_retries));

            if (build(blueprints, plan))
            {
                priorities = plan.priorities;
                stock_goals = plan.stock_goals;
                military_min = plan.military_min;
                military_max = plan.military_max;
                DFAI_DEBUG(blueprint, 1, "Successfully created a blueprint using plan: " << plan.name);
                return true;
            }

            DFAI_DEBUG(blueprint, 1, "Plan failed. Resetting.");
            clear();
        }
    }

    DFAI_DEBUG(blueprint, 1, "Ran out of plans. Failed to create a blueprint.");
    return false;
}

void PlanSetup::create_from_blueprint(room * & fort_entrance, std::vector<room *> & real_rooms_and_corridors, std::vector<plan_priority_t> & real_priorities) const
{
    std::vector<furniture *> real_layout;

    for (auto f : layout)
    {
        real_layout.push_back(f ? new furniture() : nullptr);
    }
    for (auto r : rooms)
    {
        real_rooms_and_corridors.push_back(r ? new room(room_type::type(), df::coord(), df::coord()) : nullptr);
    }

    for (size_t i = 0; i < layout.size(); i++)
    {
        auto in = layout.at(i);
        auto out = real_layout.at(i);

        if (!in)
            continue;

        out->type = in->type;
        out->construction = in->construction;
        out->dig = in->dig;
        out->pos = in->pos;

        if (in->has_target)
        {
            out->target = real_layout.at(in->target);
        }
        else
        {
            out->target = nullptr;
        }

        out->has_users = in->has_users;
        out->ignore = in->ignore;
        out->makeroom = in->makeroom;
        out->internal = in->internal;

        out->comment = in->comment(in->context);
    }
    for (size_t i = 0; i < rooms.size(); i++)
    {
        auto in = rooms.at(i);
        auto out = real_rooms_and_corridors.at(i);

        if (!in)
            continue;

        out->type = in->type;

        out->corridor_type = in->corridor_type;
        out->farm_type = in->farm_type;
        out->stockpile_type = in->stockpile_type;
        out->nobleroom_type = in->nobleroom_type;
        out->outpost_type = in->outpost_type;
        out->location_type = in->location_type;
        out->cistern_type = in->cistern_type;
        out->workshop_type = in->workshop_type;
        out->furnace_type = in->furnace_type;

        out->raw_type = in->raw_type(in->context);

        out->comment = in->comment(in->context);

        out->min = in->min;
        out->max = in->max;

        for (auto ri : in->accesspath)
        {
            out->accesspath.push_back(real_rooms_and_corridors.at(ri));
        }
        for (auto fi : in->layout)
        {
            out->layout.push_back(real_layout.at(fi));
        }

        out->level = in->level;
        out->noblesuite = in->noblesuite;
        out->queue = in->queue;

        if (in->has_workshop)
        {
            out->workshop = real_rooms_and_corridors.at(in->workshop);
        }
        else
        {
            out->workshop = nullptr;
        }

        out->stock_disable = in->stock_disable;
        out->stock_specific1 = in->stock_specific1;
        out->stock_specific2 = in->stock_specific2;

        out->has_users = in->has_users;
        out->temporary = in->temporary;
        out->outdoor = in->outdoor;
        out->build_when_accessible = in->build_when_accessible;

        if (in->outdoor && !in->require_floor)
        {
            for (df::coord t = in->min; t.x <= in->max.x; t.x++)
            {
                for (t.y = in->min.y; t.y <= in->max.y; t.y++)
                {
                    int16_t z = Plan::surface_tile_at(t.x, t.y, true).z;
                    for (t.z = in->min.z; t.z <= in->max.z; t.z++)
                    {
                        bool has_layout = false;
                        for (auto f : out->layout)
                        {
                            if (in->min + f->pos == t)
                            {
                                has_layout = true;
                                break;
                            }
                        }
                        if (!has_layout && (t.z != z || !in->require_grass))
                        {
                            furniture *f = new furniture();
                            if (t.z == z)
                            {
                                f->construction = construction_type::Floor;
                            }
                            else
                            {
                                f->dig = tile_dig_designation::Channel;
                            }
                            f->pos = t - in->min;
                            out->layout.push_back(f);
                        }
                    }
                }
            }
        }
    }

    auto real_rooms_end = std::remove(real_rooms_and_corridors.begin(), real_rooms_and_corridors.end(), nullptr);
    real_rooms_and_corridors.erase(real_rooms_end, real_rooms_and_corridors.end());

    fort_entrance = real_rooms_and_corridors.at(0);
    std::sort(real_rooms_and_corridors.begin(), real_rooms_and_corridors.end(), [fort_entrance](const room *a, const room *b) -> bool
    {
        if (b == fort_entrance)
        {
            return false;
        }
        if (a == fort_entrance)
        {
            return true;
        }
        df::coord da(fort_entrance->pos() - a->pos());
        df::coord db(fort_entrance->pos() - b->pos());
        df::coord da_abs(std::abs(da.x), std::abs(da.y), std::abs(da.z));
        df::coord db_abs(std::abs(db.x), std::abs(db.y), std::abs(db.z));
        if (da_abs.z != db_abs.z)
        {
            return da_abs.z < db_abs.z;
        }
        if (da.z != db.z)
        {
            return da.z < db.z;
        }
        if (da_abs.x != db_abs.x)
        {
            return da_abs.x < db_abs.x;
        }
        if (da.x != db.x)
        {
            return da.x < db.x;
        }
        if (da_abs.y != db_abs.y)
        {
            return da_abs.y < db_abs.y;
        }
        if (da.y != db.y)
        {
            return da.y < db.y;
        }
        return false;
    });
    real_priorities = priorities;
    Watch = stock_goals;
    ai.pop.military_min = military_min;
    ai.pop.military_max = military_max;
}
