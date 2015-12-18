#include "ai.h"

#include "modules/Gui.h"
#include "modules/Units.h"

#include "df/building.h"
#include "df/building_civzonest.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/entity_position_raw.h"
#include "df/entity_raw.h"
#include "df/general_ref.h"
#include "df/general_ref_building_civzone_assignedst.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/histfig_entity_link_positionst.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/interface_key.h"
#include "df/itemdef_weaponst.h"
#include "df/job.h"
#include "df/manager_order.h"
#include "df/squad.h"
#include "df/squad_ammo_spec.h"
#include "df/squad_order_kill_listst.h"
#include "df/squad_order_trainst.h"
#include "df/squad_position.h"
#include "df/squad_schedule_order.h"
#include "df/squad_uniform_spec.h"
#include "df/ui.h"
#include "df/uniform_category.h"
#include "df/unit.h"
#include "df/unit_misc_trait.h"
#include "df/unit_skill.h"
#include "df/unit_soul.h"
#include "df/unit_wound.h"
#include "df/viewscreen.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(standing_orders_forbid_used_ammo);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

Population::Population(color_ostream & out, AI *parent) :
    ai(parent),
    citizens(),
    military(),
    idlers(),
    pets(),
    update_tick(0)
{
    *standing_orders_forbid_used_ammo = 0;
}

Population::~Population()
{
}

command_result Population::status(color_ostream & out)
{
    out << citizens.size() << " citizen, ";
    out << military.size() << " military, ";
    out << idlers.size() << " idle, ";
    out << pets.size() << " pets\n";
    return CR_OK;
}

command_result Population::statechange(color_ostream & out, state_change_event event)
{
    return CR_OK;
}

command_result Population::update(color_ostream & out)
{
    update_tick++;
    if (update_tick >= 300)
    {
        update_tick -= 300;
    }
    switch (update_tick % 100)
    {
        case 10:
            update_citizenlist(out);
            break;
        case 20:
            update_nobles(out);
            break;
        case 30:
            update_jobs(out);
            break;
        case 40:
            update_military(out);
            break;
        case 50:
            update_pets(out);
            break;
        case 60:
            update_deads(out);
            break;
        case 70:
            update_caged(out);
            break;
        default:
            break;
    }

    switch (update_tick % 30)
    {
        case 3:
            autolabors_workers(out);
            break;
        case 6:
            autolabors_jobs(out);
            break;
        case 9:
            autolabors_labors(out);
            break;
        case 12:
            autolabors_commit(out);
            break;
        default:
            break;
    }

    return CR_OK;
}

void Population::update_citizenlist(color_ostream & out)
{
    std::set<int32_t> old = citizens;

    // add new fort citizen to our list
    for (auto unit : world->units.active)
    {
        if (!Units::isCitizen(unit))
            continue;

        if (Units::isBaby(unit))
            continue;

        if (citizens.insert(unit->id).second)
        {
            ai->plan.new_citizen(out, unit->id);
        }
        old.erase(unit->id);
    }

    // del those who are no longer here
    for (auto id : old)
    {
        // u.counters.death_tg.flags.discovered dead/missing
        citizens.erase(id);
        military.erase(id);
        ai->plan.del_citizen(out, id);
    }
}

static int32_t total_xp(df::unit *unit)
{
    int32_t total = 0;
    for (auto skill : unit->status.current_soul->skills)
    {
        total += 400 * skill->rating + 100 * skill->rating * (skill->rating + 1) / 2 + skill->experience;
    }
    return total;
}

static bool compare_total_xp(df::unit *a, df::unit *b)
{
    return total_xp(a) < total_xp(b);
}

static const std::string *position_code(df::historical_entity *ent, df::entity_position_responsibility r)
{
    for (auto pos : ent->entity_raw->positions)
    {
        if (pos->responsibilities[r])
        {
            return &pos->code;
        }
    }
    return nullptr;
}

static df::entity_position_assignment *assign_new_noble_by_code(AI *ai, color_ostream & out, const std::string *pos_code, df::unit *unit)
{
    df::historical_entity *ent = ui->main.fortress_entity;

    for (auto pos : ent->positions.own)
    {
        if (pos->code != *pos_code)
            continue;

        auto assign = std::find_if(ent->positions.assignments.begin(), ent->positions.assignments.end(), [pos](df::entity_position_assignment *a) -> bool { return a->position_id == pos->id && a->histfig == -1; });
        if (assign == ent->positions.assignments.end())
        {
            int32_t a_id = ent->positions.next_assignment_id++;
            df::entity_position_assignment *newassign = df::allocate<df::entity_position_assignment>();
            newassign->id = a_id;
            newassign->position_id = pos->id;
            newassign->flags.set(0); // XXX
            assign = ent->positions.assignments.insert(assign, newassign);
        }

        df::histfig_entity_link_positionst *poslink = df::allocate<df::histfig_entity_link_positionst>();
        poslink->link_strength = 100;
        poslink->start_year = *cur_year;
        poslink->entity_id = ent->id;
        poslink->assignment_id = (*assign)->id;

        df::historical_figure *hf = df::historical_figure::find(unit->hist_figure_id);
        hf->entity_links.push_back(poslink);
        (*assign)->histfig = unit->hist_figure_id;

        FOR_ENUM_ITEMS(entity_position_responsibility, r)
        {
            if (pos->responsibilities[r])
            {
                ent->assignments_by_type[r].push_back(*assign);
            }
        }

        return *assign;
    }
    return nullptr;
}

static df::entity_position_assignment *assign_new_noble(AI *ai, color_ostream & out, df::entity_position_responsibility r, df::unit *unit)
{
    df::historical_entity *ent = ui->main.fortress_entity;

    const std::string *pos_code = position_code(ent, r);
    if (pos_code == nullptr)
    {
        ai->debug(out, std::string("could not find position with responsibility ") + enum_item_key_str(r));
        return nullptr;
    }

    df::entity_position_assignment *assign = assign_new_noble_by_code(ai, out, pos_code, unit);
    if (!assign)
    {
        ai->debug(out, "could not find position '" + *pos_code + "' with responsibility " + enum_item_key_str(r));
    }
    return assign;
}

static df::entity_position_assignment *assign_new_noble_commander(AI *ai, color_ostream & out, df::unit *unit)
{
    df::historical_entity *ent = ui->main.fortress_entity;

    for (auto pos : ent->entity_raw->positions)
    {
        if (pos->responsibilities[entity_position_responsibility::MILITARY_STRATEGY] && pos->flags.is_set(entity_position_raw_flags::SITE))
        {
            df::entity_position_assignment *assign = assign_new_noble_by_code(ai, out, &pos->code, unit);
            if (!assign)
            {
                ai->debug(out, "could not find position '" + pos->code + "' with responsibility MILITARY_STRATEGY (site)");
            }
            return assign;
        }
    }
    ai->debug(out, "could not find position with responsibility MILITARY_STRATEGY (site)");
    return nullptr;
}

static df::entity_position_assignment *assign_new_noble_captain(AI *ai, color_ostream & out, df::unit *unit)
{
    df::historical_entity *ent = ui->main.fortress_entity;

    for (auto pos : ent->entity_raw->positions)
    {
        if (pos->flags.is_set(entity_position_raw_flags::MILITARY_SCREEN_ONLY) && pos->flags.is_set(entity_position_raw_flags::SITE))
        {
            df::entity_position_assignment *assign = assign_new_noble_by_code(ai, out, &pos->code, unit);
            if (!assign)
            {
                ai->debug(out, "could not find position '" + pos->code + "' for captain (site)");
            }
            return assign;
        }
    }
    ai->debug(out, "could not find position for captain (site)");
    return nullptr;
}

void Population::update_nobles(color_ostream & out)
{
    std::vector<df::unit *> cz;
    for (int32_t id : citizens)
    {
        df::unit *unit = df::unit::find(id);
        if (Units::isChild(unit))
            continue;

        cz.push_back(unit);
    }
    std::sort(cz.begin(), cz.end(), compare_total_xp);

    if (cz.empty())
        return;

    df::historical_entity *ent = ui->main.fortress_entity;

    if (ent->assignments_by_type[entity_position_responsibility::MANAGE_PRODUCTION].empty())
    {
        df::unit *best = cz[0];
        for (auto unit : cz)
        {
            if (unit->military.squad_id == -1 && std::find_if(ent->positions.assignments.begin(), ent->positions.assignments.end(), [unit](df::entity_position_assignment *a) -> bool { return a->histfig == unit->hist_figure_id; }) == ent->positions.assignments.end())
            {
                best = unit;
                break;
            }
        }
        assign_new_noble(ai, out, entity_position_responsibility::MANAGE_PRODUCTION, best);
    }

    if (ent->assignments_by_type[entity_position_responsibility::ACCOUNTING].empty())
    {
        df::unit *best = nullptr;
        for (auto unit : cz)
        {
            if (!unit->status.labors[unit_labor::MINE] && unit->military.squad_id == -1 && std::find_if(ent->positions.assignments.begin(), ent->positions.assignments.end(), [unit](df::entity_position_assignment *a) -> bool { return a->histfig == unit->hist_figure_id; }) == ent->positions.assignments.end())
            {
                best = unit;
                break;
            }
        }
        if (best)
        {
            assign_new_noble(ai, out, entity_position_responsibility::ACCOUNTING, best);
            ui->bookkeeper_settings = 4;
        }
    }

    if (ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].empty())
    {
        auto hospital = ai->plan.find_room(Plan::room_type::infirmary);
        if (hospital && hospital->status != Plan::room_status::plan)
        {
            df::unit *best = nullptr;
            for (auto unit : cz)
            {
                if (unit->military.squad_id == -1 && std::find_if(ent->positions.assignments.begin(), ent->positions.assignments.end(), [unit](df::entity_position_assignment *a) -> bool { return a->histfig == unit->hist_figure_id; }) == ent->positions.assignments.end())
                {
                    best = unit;
                    break;
                }
            }
            if (best)
            {
                assign_new_noble(ai, out, entity_position_responsibility::HEALTH_MANAGEMENT, best);
            }
        }
    }
    else
    {
        auto assignment = ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT][0];
        df::historical_figure *hf = df::historical_figure::find(assignment->histfig);
        if (hf)
        {
            df::unit *doctor = df::unit::find(hf->unit_id);
            if (doctor)
            {
                // doc => healthcare
                doctor->status.labors[unit_labor::DIAGNOSE] = true;
                doctor->status.labors[unit_labor::SURGERY] = true;
                doctor->status.labors[unit_labor::BONE_SETTING] = true;
                doctor->status.labors[unit_labor::SUTURING] = true;
                doctor->status.labors[unit_labor::DRESSING_WOUNDS] = true;
                doctor->status.labors[unit_labor::FEED_WATER_CIVILIANS] = true;
            }
        }
    }

    if (ent->assignments_by_type[entity_position_responsibility::TRADE].empty())
    {
        df::unit *best = nullptr;
        for (auto unit : cz)
        {
            if (unit->military.squad_id == -1 && std::find_if(ent->positions.assignments.begin(), ent->positions.assignments.end(), [unit](df::entity_position_assignment *a) -> bool { return a->histfig == unit->hist_figure_id; }) == ent->positions.assignments.end())
            {
                best = unit;
                break;
            }
        }
        if (best)
        {
            assign_new_noble(ai, out, entity_position_responsibility::TRADE, best);
        }
    }

    std::set<int32_t> nobles;
    for (auto assignment : ent->positions.assignments)
    {
        if (assignment->histfig == -1)
            continue;

        df::entity_position *pos = binsearch_in_vector(ent->positions.own, assignment->position_id);
        if (pos->required_office > 0 || pos->required_dining > 0 || pos->required_tomb > 0)
        {
            nobles.insert(df::historical_figure::find(assignment->histfig)->unit_id);
        }
    }
    ai->plan.attribute_noblerooms(out, nobles);
}

void Population::update_jobs(color_ostream & out)
{
    for (df::job_list_link *job = world->job_list.next; job; job = job->next)
    {
        if (!job->item->flags.bits.repeat)
        {
            job->item->flags.bits.suspend = 0;
        }
    }
}

void Population::update_military(color_ostream & out)
{
    df::historical_entity *ent = ui->main.fortress_entity;

    // check for new soldiers, allocate barracks
    std::vector<df::unit *> newsoldiers;
    std::vector<df::unit *> maydraft;

    for (auto id : citizens)
    {
        df::unit *unit = df::unit::find(id);
        if (unit->military.squad_id == 1)
        {
            if (military.erase(id))
            {
                ai->plan.del_soldier(out, id);
            }

            if (!Units::isChild(unit) && unit->mood == mood_type::None)
            {
                maydraft.push_back(unit);
            }
        }
        else if (military.insert(std::make_pair(id, unit->military.squad_id)).second)
        {
            newsoldiers.push_back(unit);
        }
    }
    std::sort(maydraft.begin(), maydraft.end(), compare_total_xp);

    // enlist new soldiers if needed
    for (int i = 0; military.size() < (maydraft.size() - i) / 4; i++)
    {
        df::unit *unit = maydraft[i];

        int squad_size = 8;
        if (military.size() < 4 * 6)
            squad_size = 6;
        if (military.size() < 3 * 4)
            squad_size = 4;

        int32_t squad_id = -1;

        for (auto sq : ent->squads)
        {
            int count = 0;
            for (auto soldier : military)
            {
                if (soldier.second == sq)
                {
                    count++;
                }
            }

            if (count < squad_size)
            {
                squad_id = sq;
                break;
            }
        }

        if (squad_id == -1)
        {
            // create a new squad using the UI
            std::set<df::interface_key> keys;
#define KEY(key) \
            keys.clear(); \
            keys.insert(interface_key::key); \
            Gui::getCurViewscreen()->feed(&keys)
            KEY(D_MILITARY);
            KEY(D_MILITARY_CREATE_SQUAD);
            KEY(STANDARDSCROLL_UP);
            KEY(SELECT);
            KEY(LEAVESCREEN);
#undef KEY

            // get the squad and its id
            squad_id = ent->squads.back();
            df::squad *squad = df::squad::find(squad_id);

            squad->cur_alert_idx = 1; // train
            squad->uniform_priority = 2;
            squad->carry_food = 2;
            squad->carry_water = 2;

            // uniform
            for (auto pos : squad->positions)
            {
#define UNIFORM(type, item, cat) \
                if (pos->uniform[uniform_category::type].empty()) \
                { \
                    df::squad_uniform_spec *spec = df::allocate<df::squad_uniform_spec>(); \
                    spec->color = -1; \
                    spec->item_filter.item_type = item_type::item; \
                    spec->item_filter.material_class = entity_material_category::cat; \
                    spec->item_filter.mattype = -1; \
                    spec->item_filter.matindex = -1; \
                    pos->uniform[uniform_category::type].push_back(spec); \
                }
                UNIFORM(body, ARMOR, Armor);
                UNIFORM(head, HELM, Armor);
                UNIFORM(pants, PANTS, Armor);
                UNIFORM(gloves, GLOVES, Armor);
                UNIFORM(shoes, SHOES, Armor);
                UNIFORM(shield, SHIELD, Armor);
                UNIFORM(weapon, WEAPON, None);
#undef UNIFORM
                pos->flags.bits.exact_matches = 1;
            }

            if (ent->squads.size() % 3 == 0)
            {
                // ranged squad
                for (auto pos : squad->positions)
                {
                    pos->uniform[uniform_category::weapon][0]->indiv_choice.bits.ranged = 1;
                }
                df::squad_ammo_spec *ammo = df::allocate<df::squad_ammo_spec>();
                ammo->item_filter.item_type = item_type::AMMO;
                ammo->item_filter.item_subtype = 0; // TODO: check raws for actual bolt subtype
                ammo->item_filter.material_class = entity_material_category::None;
                ammo->amount = 500;
                ammo->flags.bits.use_combat = 1;
                ammo->flags.bits.use_training = 1;
                squad->ammunition.push_back(ammo);
            }
            else
            {
                // we don't want all the axes being used up by the military.
                std::vector<df::itemdef_weaponst *> weapons;
                for (int16_t id : ent->entity_raw->equipment.weapon_id)
                {
                    df::itemdef_weaponst *weapon = df::itemdef_weaponst::find(id);
                    if (!weapon->flags.is_set(weapon_flags::TRAINING) &&
                            weapon->skill_melee != job_skill::MINING &&
                            weapon->skill_melee != job_skill::AXE &&
                            weapon->skill_ranged == job_skill::NONE)
                        weapons.push_back(weapon);
                }
                if (weapons.empty())
                {
                    ai->debug(out, "squad: no melee weapons found");
                    for (auto pos : squad->positions)
                    {
                        pos->uniform[uniform_category::weapon][0]->indiv_choice.bits.melee = 1;
                    }
                }
                else
                {
                    size_t weapon_index = (ent->squads.size() - 1) * 10;
                    for (auto pos : squad->positions)
                    {
                        pos->uniform[uniform_category::weapon][0]->item_filter.item_subtype = weapons[weapon_index++ % weapons.size()]->subtype;
                    }
                }
            }
        }

        df::squad *squad = df::squad::find(squad_id);
        int32_t squad_position = -1;
        for (auto pos : squad->positions)
        {
            squad_position++;
            if (pos->occupant != -1)
                continue;

            pos->occupant = unit->hist_figure_id;
            unit->military.squad_id = squad_id;
            unit->military.squad_position = squad_position;

            bool has_leader = false;
            for (auto assign : ent->positions.assignments)
            {
                if (assign->squad_id == squad_id)
                {
                    has_leader = true;
                    break;
                }
            }

            if (!has_leader)
            {
                df::entity_position_assignment *assign = ent->assignments_by_type[entity_position_responsibility::MILITARY_STRATEGY].empty() ? assign_new_noble_commander(ai, out, unit) : assign_new_noble_captain(ai, out, unit);
                if (assign)
                    assign->squad_id = squad_id;
            }
        }
        military[unit->id] = squad_id;
        newsoldiers.push_back(unit);
    }

    for (auto unit : newsoldiers)
    {
        ai->plan.new_soldier(out, unit->id);
    }

    for (auto id : ent->squads)
    {
        df::squad *squad = df::squad::find(id);
        int soldier_count = 0;
        for (auto pos : squad->positions)
        {
            if (pos->occupant != -1)
                soldier_count++;
        }
        for (int month = 0; month < 12; month++)
        {
            for (auto order : squad->schedule[1][month]->orders)
            {
                df::squad_order_trainst *train = virtual_cast<df::squad_order_trainst>(order->order);
                if (train)
                {
                    order->min_count = (soldier_count > 3 ? soldier_count - 1 : soldier_count);
                    auto barracks = ai->plan.find_room(Plan::room_type::barracks, [id](Plan::room *r) -> bool { return r->info.barracks.squad_id == id; });
                    if (barracks && barracks->status != Plan::room_status::finished)
                    {
                        order->min_count = 0;
                    }
                }
            }
        }
    }
}

static void assign_unit_to_zone(df::unit *unit, Plan::room *pasture)
{
    // remove existing zone assignments
    // TODO remove existing chains/cages?
    unit->general_refs.erase(std::remove_if(unit->general_refs.begin(), unit->general_refs.end(), [unit](df::general_ref *gen) -> bool
        {
            df::general_ref_building_civzone_assignedst *ref = virtual_cast<df::general_ref_building_civzone_assignedst>(gen);
            if (ref)
            {
                df::building_civzonest *zone = virtual_cast<df::building_civzonest>(ref->getBuilding());
                zone->assigned_units.erase(std::find(zone->assigned_units.begin(), zone->assigned_units.end(), unit->id));
                delete ref;
                return true;
            }
            return false;
        }), unit->general_refs.end());

    df::general_ref_building_civzone_assignedst *ref = df::allocate<df::general_ref_building_civzone_assignedst>();
    ref->building_id = pasture->building_id;
    unit->general_refs.push_back(ref);
    virtual_cast<df::building_civzonest>(ref->getBuilding())->assigned_units.push_back(unit->id);
}

void Population::update_pets(color_ostream & out)
{
    int needmilk = 0;
    int needshear = 0;

    for (auto order : ai->stocks.find_manager_orders(job_type::MilkCreature))
    {
        needmilk -= order->amount_left;
    }

    for (auto order : ai->stocks.find_manager_orders(job_type::ShearCreature))
    {
        needshear -= order->amount_left;
    }

    std::map<df::caste_raw *, std::vector<std::pair<int32_t, df::unit *> > > forSlaughter;

    std::map<int32_t, pet_flags> old = pets;
    for (auto unit : world->units.active)
    {
        if (unit->civ_id != ui->civ_id ||
                unit->race == ui->race_id ||
                unit->flags1.bits.dead ||
                unit->flags1.bits.merchant ||
                unit->flags1.bits.forest ||
                unit->flags2.bits.slaughter)
            continue;

        df::creature_raw *race = world->raws.creatures.all[unit->race];
        df::caste_raw *caste = race->caste[unit->caste];
        int32_t age = (*cur_year - unit->relations.birth_year) * 12 * 28 + (*cur_year_tick - unit->relations.birth_time) / 1200; // days

        if (pets.count(unit->id))
        {
            if (caste->body_size_2.back() <= age && // full grown
                    unit->profession != profession::TRAINED_HUNTER && // not trained
                    unit->profession != profession::TRAINED_WAR && // not trained
                    unit->relations.pet_owner_id == -1) // not owned
            {
                bool is_gelded = false;
                for (auto wound : unit->body.wounds)
                {
                    for (auto part : wound->parts)
                    {
                        if (part->flags2.bits.gelded)
                        {
                            is_gelded = true;
                            break;
                        }
                    }
                }

                if (is_gelded || // Bob Barker'd
                        (caste->gender == 0 && !unit->status.current_soul->orientation_flags.bits.marry_male) || // Ellen Degeneres'd
                        (caste->gender == 1 && !unit->status.current_soul->orientation_flags.bits.marry_female)) // Neil Patrick Harris'd
                {
                    // animal can't reproduce, can't work, and will provide
                    // maximum butchering reward. kill it.
                    unit->flags2.bits.slaughter = 1;
                    std::ostringstream message;
                    message << "[pop] marked ";
                    message << age / 12 / 28 << "y";
                    message << age % (12 * 28) << "d old ";
                    message << race->creature_id << ":" << caste->caste_id;
                    message << " for slaughter (can't reproduce)";
                    ai->debug(out, message.str());
                    continue;
                }

                forSlaughter[caste].push_back(std::make_pair(age, unit));
            }

            pet_flags flags = pets.at(unit->id);
            bool is_adult = unit->profession != profession::BABY && unit->profession != profession::CHILD;
            if (flags.bits.milkable && is_adult)
            {
                bool milked = false;
                for (auto trait : unit->status.misc_traits)
                {
                    if (trait->id == misc_trait_type::MilkCounter)
                    {
                        milked = true;
                        break;
                    }
                }
                if (!milked)
                    needmilk++;
            }

            if (flags.bits.shearable && is_adult)
            {
                bool shearable = false;
                for (auto layer : caste->shearable_tissue_layer)
                {
                    for (auto index : layer->bp_modifiers_idx)
                    {
                        if (unit->appearance.bp_modifiers[index] >= layer->length)
                        {
                            shearable = true;
                            break;
                        }
                    }
                    if (shearable)
                        break;
                }
                if (shearable)
                    needshear++;
            }

            old.erase(unit->id);

            continue;
        }

        pet_flags flags;
        if (caste->flags.is_set(caste_raw_flags::MILKABLE))
        {
            flags.bits.milkable = 1;
        }
        if (!caste->shearable_tissue_layer.empty())
        {
            flags.bits.shearable = 1;
        }
        if (caste->flags.is_set(caste_raw_flags::HUNTS_VERMIN))
        {
            flags.bits.hunts_vermin = 1;
        }
        if (caste->flags.is_set(caste_raw_flags::GRAZER))
        {
            flags.bits.grazer = 1;

            auto pasture = ai->plan.new_grazer(out, unit->id);
            if (pasture)
            {
                assign_unit_to_zone(unit, pasture);
                // TODO monitor grass levels
            }
            else
            {
                // TODO slaughter best candidate, keep this one
                // also avoid killing named pets
                unit->flags2.bits.slaughter = 1;
                std::ostringstream message;
                message << "[pop] marked ";
                message << age / 12 / 28 << "y";
                message << age % (12 * 28) << "d old ";
                message << race->creature_id << ":" << caste->caste_id;
                message << " for slaughter (no pasture)";
                ai->debug(out, message.str());
            }
        }
        pets[unit->id] = flags;
    }

    for (auto remove : old)
    {
        ai->plan.del_grazer(out, remove.first);
        pets.erase(remove.first);
    }

    for (auto slaughter : forSlaughter)
    {
        df::caste_raw *caste = slaughter.first;
        std::vector<std::pair<int32_t, df::unit *>> & candidates = slaughter.second;

        std::sort(candidates.begin(), candidates.end(), [](std::pair<int32_t, df::unit *> a, std::pair<int32_t, df::unit *> b) -> bool { return a.first > b.first; });

        for (size_t i = 0; i < candidates.size() - 5; i++)
        {
            // We have reproductively viable animals, but there are more than
            // five of this sex (full-grown). Kill the oldest ones for meat,
            // leather, and bones.

            int32_t age = candidates[i].first;
            df::unit *unit = candidates[i].second;
            df::creature_raw *race = world->raws.creatures.all[unit->race];

            std::ostringstream message;
            message << "[pop] marked ";
            message << age / 12 / 28 << "y";
            message << age % (12 * 28) << "d old ";
            message << race->creature_id << ":" << caste->caste_id;
            message << " for slaughter (too many adults)";
            ai->debug(out, message.str());
        }
    }

    if (needmilk > 10)
        needmilk = 10;
    if (needmilk > 0)
        ai->stocks.add_manager_order(job_type::MilkCreature, needmilk);

    if (needshear > 10)
        needshear = 10;
    if (needshear > 0)
        ai->stocks.add_manager_order(job_type::ShearCreature, needshear);
}

void Population::update_deads(color_ostream & out)
{
    for (auto unit : world->units.active)
    {
        if (unit->flags3.bits.ghostly)
        {
            ai->stocks.queue_slab(unit->hist_figure_id);
        }
    }
}

static void military_random_squad_attack_unit(df::unit *unit, const std::string & desc)
{
    df::squad *squad = nullptr;
    int32_t best = 0;
    for (auto id : ui->main.fortress_entity->squads)
    {
        df::squad *candidate = df::squad::find(id);
        int32_t score = 0;
        for (auto pos : candidate->positions)
        {
            if (pos->occupant != -1)
            {
                score++;
            }
        }
        score -= candidate->orders.size();
        if (!squad || best < score)
        {
            squad = candidate;
            best = score;
        }
    }
    if (!squad)
        return;

    df::squad_order_kill_listst *kill = df::allocate<df::squad_order_kill_listst>();
    kill->units.push_back(unit->id);
    kill->title = desc;

    squad->orders.push_back(kill);
}

void Population::update_caged(color_ostream & out)
{
    int count = 0;
    for (auto cage : world->items.other[items_other_id::CAGE])
    {
        if (!cage->flags.bits.on_ground)
            continue;
        for (auto ref : cage->general_refs)
        {
            df::general_ref_contains_itemst *item = virtual_cast<df::general_ref_contains_itemst>(ref);
            if (item)
            {
                if (!item->getItem()->flags.bits.dump)
                    count++;

                item->getItem()->flags.bits.dump = 1;
                continue;
            }

            df::general_ref_contains_unitst *prisoner = virtual_cast<df::general_ref_contains_unitst>(ref);
            if (prisoner)
            {
                df::unit *unit = prisoner->getUnit();
                std::vector<int32_t> & histfigs = ui->main.fortress_entity->histfig_ids;
                if (std::find(histfigs.begin(), histfigs.end(), unit->hist_figure_id) != histfigs.end())
                {
                    // TODO rescue caged dwarves
                }
                else
                {
                    for (auto it : unit->inventory)
                    {
                        if (!it->item->flags.bits.dump)
                            count++;

                        it->item->flags.bits.dump = 1;
                    }

                    if (unit->inventory.empty())
                    {
                        auto pit = ai->plan.find_room(Plan::room_type::pitcage, [cage](Plan::room *room) -> bool { return room->building_id != -1 && abs(room->pos.x - cage->pos.x) <= 1 && abs(room->pos.y - cage->pos.y) <= 1 && room->pos.z == cage->pos.z; });
                        if (pit)
                        {
                            assign_unit_to_zone(unit, pit);
                            df::creature_raw *race = world->raws.creatures.all[unit->race];
                            df::caste_raw *caste = race->caste[unit->caste];
                            std::ostringstream desc;
                            desc << race->creature_id << ":" << caste->caste_id;
                            military_random_squad_attack_unit(unit, desc.str());
                            std::ostringstream message;
                            message << "[pop] marked ";
                            message << desc.str();
                            message << " for pitting";
                            ai->debug(out, message.str());
                        }
                    }
                }
            }
        }
    }
    if (count > 0)
    {
        std::ostringstream message;
        message << "[pop] dumped " << count << " items from cages";
        ai->debug(out, message.str());
    }
}


/*
class DwarfAI
    class Population
        LaborList = DFHack::UnitLabor::ENUM.sort.transpose[1] - [:NONE]
        LaborTool = { :MINE => true, :CUTWOOD => true, :HUNT => true }
        LaborSkill = DFHack::JobSkill::Labor.invert
        LaborIdle = { :PLANT => true, :HERBALISM => true, :FISH => true, :DETAIL => true }
        LaborMedical = { :DIAGNOSE => true, :SURGERY => true, :BONE_SETTING => true, :SUTURING => true, :DRESSING_WOUNDS => true, :FEED_WATER_CIVILIANS => true }
        LaborStocks = { :PLANT => [:food, :drink, :cloth], :HERBALISM => [:food, :drink, :cloth], :FISH => [:food] }
        LaborHauling = { :FEED_WATER_CIVILIANS => true, :RECOVER_WOUNDED => true }
        LaborList.each { |lb|
            if lb.to_s =~ /HAUL/
                LaborHauling[lb] = true
            end
        }

        LaborMin = Hash.new(2).update :DETAIL => 4, :PLANT => 4, :HERBALISM => 1
        LaborMax = Hash.new(8).update :FISH => 1
        LaborMinPct = Hash.new(0).update :DETAIL => 5, :PLANT => 30, :FISH => 1, :HERBALISM => 10
        LaborMaxPct = Hash.new(0).update :DETAIL => 20, :PLANT => 60, :FISH => 10, :HERBALISM => 30
        LaborHauling.keys.each { |lb|
            LaborMinPct[lb] = 30
            LaborMaxPct[lb] = 100
        }
        LaborWontWorkJob = { :AttendParty => true, :Rest => true, :UpdateStockpileRecords => true }

        def autolabors(step)
            case step
            when 1
                @workers = []
                @idlers = []
                @labor_needmore = Hash.new(0)
                nonworkers = []
                @medic = {}
                df.ui.main.fortress_entity.assignments_by_type[:HEALTH_MANAGEMENT].each { |n|
                    next unless hf = df.world.history.figures.binsearch(n.histfig)
                    @medic[hf.unit_id] = true
                }

                merchant = df.world.units.all.any? { |u|
                    u.flags1.merchant and not u.flags1.dead
                }

                set_up_trading(merchant)

                citizen.each_value { |c|
                    next if not u = c.dfunit
                    if u.mood != :None
                        nonworkers << [c, "has mood: #{u.mood}"]
                    elsif u.profession == :CHILD
                        nonworkers << [c, 'is a child', true]
                    elsif u.profession == :BABY
                        nonworkers << [c, 'is a baby', true]
                    elsif unit_hasmilitaryduty(u)
                        nonworkers << [c, 'has military duty']
                    elsif u.flags1.caged
                        nonworkers << [c, 'caged']
                    elsif @citizen.length >= 20 and
                        df.world.manager_orders.last and
                        df.world.manager_orders.last.is_validated == 0 and
                        df.unit_entitypositions(u).find { |n| n.responsibilities[:MANAGE_PRODUCTION] }
                        nonworkers << [c, 'validating work orders']
                    elsif merchant and df.unit_entitypositions(u).find { |n| n.responsibilities[:TRADE] }
                        nonworkers << [c, 'trading']
                    elsif u.job.current_job and LaborWontWorkJob[u.job.current_job.job_type]
                        nonworkers << [c, DFHack::JobType::Caption[u.job.current_job.job_type]]
                    elsif u.status.misc_traits.find { |mt| mt.id == :OnBreak }
                        nonworkers << [c, 'on break']
                    elsif u.specific_refs.find { |sr| sr.type == :ACTIVITY }
                        nonworkers << [c, 'has activity']
                    else
                        # TODO filter nobles that will not work
                        @workers << c
                        @idlers << c if not u.job.current_job
                    end
                }

                # free non-workers
                nonworkers.each { |c|
                    u = c[0].dfunit
                    ul = u.status.labors
                    LaborList.each { |lb|
                        if LaborHauling[lb]
                            if not ul[lb] and not c[2]
                                ul[lb] = true
                                u.military.pickup_flags.update = true if LaborTool[lb]
                                ai.debug "assigning labor #{lb} to #{DwarfAI::describe_unit(u)} (non-worker: #{c[1]})"
                            end
                        elsif ul[lb]
                            next if LaborMedical[lb] and @medic[u.id]
                            ul[lb] = false
                            u.military.pickup_flags.update = true if LaborTool[lb]
                            ai.debug "unassigning labor #{lb} from #{DwarfAI::describe_unit(u)} (non-worker: #{c[1]})"
                        end
                    }
                }

            when 2
                seen_workshop = {}
                df.world.job_list.each { |job|
                    ref_bld = job.general_refs.grep(DFHack::GeneralRefBuildingHolderst).first
                    ref_wrk = job.general_refs.grep(DFHack::GeneralRefUnitWorkerst).first
                    next if ref_bld and seen_workshop[ref_bld.building_id]

                    if not ref_wrk
                        job_labor = DFHack::JobSkill::Labor[DFHack::JobType::Skill[job.job_type]]
                        job_labor = DFHack::JobType::Labor.fetch(job.job_type, job_labor)
                        if job_labor and job_labor != :NONE
                            @labor_needmore[job_labor] += 1

                        else
                            case job.job_type
                            when :ConstructBuilding, :DestroyBuilding
                                # TODO
                                @labor_needmore[:UNKNOWN_BUILDING_LABOR_PLACEHOLDER] += 1
                            when :CustomReaction
                                reac = df.world.raws.reactions.find { |r| r.code == job.reaction_name }
                                if reac and job_labor = DFHack::JobSkill::Labor[reac.skill]
                                    @labor_needmore[job_labor] += 1 if job_labor != :NONE
                                end
                            when :PenSmallAnimal, :PenLargeAnimal
                                @labor_needmore[:HAUL_ANIMAL] += 1
                            when :StoreItemInStockpile, :StoreItemInBag, :StoreItemInHospital,
                                    :StoreItemInChest, :StoreItemInCabinet, :StoreWeapon,
                                    :StoreArmor, :StoreItemInBarrel, :StoreItemInBin, :StoreItemInVehicle
                                LaborHauling.keys.each { |lb|
                                    @labor_needmore[lb] += 1
                                }
                            else
                                if job.material_category.wood
                                    @labor_needmore[:CARPENTER] += 1
                                elsif job.material_category.bone
                                    @labor_needmore[:BONE_CARVE] += 1
                                elsif job.material_category.shell
                                    @labor_needmore[:BONE_CARVE] += 1
                                elsif job.material_category.cloth
                                    @labor_needmore[:CLOTHESMAKER] += 1
                                elsif job.material_category.leather
                                    @labor_needmore[:LEATHER] += 1
                                elsif job.mat_type == 0
                                    # XXX metalcraft ?
                                    @labor_needmore[:MASON] += 1
                                else
                                    @seen_badwork ||= {}
                                    ai.debug "unknown labor for #{job.job_type} #{job.inspect}" if not @seen_badwork[job.job_type]
                                    @seen_badwork[job.job_type] = true
                                end
                            end
                        end
                    end

                    if ref_bld
                        case ref_bld.building_tg
                        when DFHack::BuildingFarmplotst, DFHack::BuildingStockpilest
                            # parallel work allowed
                        else
                            seen_workshop[ref_bld.building_id] = true
                        end
                    end
                }

            when 3
                # count active labors
                @labor_worker = LaborList.inject({}) { |h, lb| h.update lb => [] }
                @worker_labor = @workers.inject({}) { |h, c| h.update c.id => [] }
                @workers.each { |c|
                    ul = c.dfunit.status.labors
                    LaborList.each { |lb|
                        if ul[lb]
                            @labor_worker[lb] << c.id
                            @worker_labor[c.id] << lb
                        end
                    }
                }

                if @workers.length > 15
                    # if one has too many labors, free him up (one per round)
                    lim = 4*LaborList.length/[@workers.length, 1].max
                    lim = 4 if lim < 4
                    lim += LaborHauling.length
                    if cid = @worker_labor.keys.find { |id| @worker_labor[id].length > lim }
                        c = citizen[cid]
                        u = c.dfunit
                        ul = u.status.labors

                        LaborList.each { |lb|
                            if ul[lb]
                                next if LaborMedical[lb] and @medic[u.id]
                                @worker_labor[c.id].delete lb
                                @labor_worker[lb].delete c.id
                                ul[lb] = false
                                u.military.pickup_flags.update = true if LaborTool[lb]
                            end
                        }
                        ai.debug "unassigned all labors from #{DwarfAI::describe_unit(u)} (too many labors)"
                    end
                end

            when 4
                labormin = LaborMin
                labormax = LaborMax
                laborminpct = LaborMinPct
                labormaxpct = LaborMaxPct

                LaborList.each { |lb|
                    max = labormax[lb]
                    maxpc = labormaxpct[lb] * @workers.length / 100
                    max = maxpc if maxpc > max
                    max = 0 if LaborStocks[lb] and LaborStocks[lb].all? { |s| not ai.stocks.need_more?(s) }

                    @labor_needmore.delete lb if @labor_worker[lb].length >= max
                }

                if @labor_needmore.empty? and not @idlers.empty? and ai.plan.past_initial_phase and (not @last_idle_year or @last_idle_year != df.cur_year)
                    ai.plan.idleidle
                    @last_idle_year = df.cur_year
                end

                # handle low-number of workers + tool labors
                mintool = LaborTool.keys.inject(0) { |s, lb|
                    min = labormin[lb]
                    minpc = laborminpct[lb] * @workers.length / 100
                    min = minpc if minpc > min
                    s + min
                }
                if @workers.length < mintool
                    labormax = labormax.dup
                    LaborTool.each_key { |lb| labormax[lb] = 0 }
                    case @workers.length
                    when 0
                        # meh
                    when 1
                        # switch mine or cutwood based on time (1/2 dwarf month each)
                        if (df.cur_year_tick / (1200*28/2)) % 2 == 0
                            labormax[:MINE] = 1
                        else
                            labormax[:CUTWOOD] = 1
                        end
                    else
                        @workers.length.times { |i|
                            # divide equally between labors, with priority
                            # to mine, then wood, then hunt
                            # XXX new labortools ?
                            lb = [:MINE, :CUTWOOD, :HUNT][i%3]
                            labormax[lb] += 1
                        }
                    end
                end

                # list of dwarves with an exclusive labor
                exclusive = {}
                [
                    [:CARPENTER, lambda { ai.plan.find_room(:workshop) { |r| r.subtype == :Carpenters and r.dfbuilding and not r.dfbuilding.jobs.empty? } }],
                    [:MINE, lambda { ai.plan.digging? }],
                    [:MASON, lambda { ai.plan.find_room(:workshop) { |r| r.subtype == :Masons and r.dfbuilding and not r.dfbuilding.jobs.empty? } }],
                    [:CUTWOOD, lambda { ai.stocks.cutting_trees? }],
                    [:DETAIL, lambda { r = ai.plan.find_room(:cistern) { |_r| _r.subtype == :well } and not r.misc[:channeled] }],
                ].each { |lb, test|
                    if @workers.length > exclusive.length+2 and @labor_needmore[lb] > 0 and test[]
                        # keep last run's choice
                        cid = @labor_worker[lb].sort_by { |i| @worker_labor[i].length }.first
                        next if not cid
                        exclusive[cid] = lb
                        @idlers.delete_if { |_c| _c.id == cid }
                        c = citizen[cid]
                        @worker_labor[cid].dup.each { |llb|
                            next if llb == lb
                            next if LaborTool[llb]
                            autolabor_unsetlabor(c, llb, "has exclusive labor: #{lb}")
                        }
                    end
                }

                # autolabor!
                LaborList.each { |lb|
                    min = labormin[lb]
                    max = labormax[lb]
                    minpc = laborminpct[lb] * @workers.length / 100
                    maxpc = labormaxpct[lb] * @workers.length / 100
                    min = minpc if minpc > min
                    max = maxpc if maxpc > max
                    max = @workers.length if @labor_needmore.empty? and LaborIdle[lb]
                    max = 0 if lb == :FISH and fishery = ai.plan.find_room(:workshop) { |_r| _r.subtype == :Fishery } and fishery.status == :plan
                    max = 0 if LaborStocks[lb] and LaborStocks[lb].all? { |s| not ai.stocks.need_more?(s) }
                    min = max if min > max
                    min = @workers.length if min > @workers.length

                    cnt = @labor_worker[lb].length
                    if cnt > max
                        next if @labor_needmore.empty?
                        sk = LaborSkill[lb]
                        @labor_worker[lb] = @labor_worker[lb].sort_by { |_cid|
                            if sk
                                if usk = df.unit_find(_cid).status.current_soul.skills.find { |_usk| _usk.id == sk }
                                    DFHack::SkillRating.int(usk.rating)
                                else
                                    0
                                end
                            else
                                rand
                            end
                        }
                        (cnt-max).times {
                            cid = @labor_worker[lb].shift
                            autolabor_unsetlabor(citizen[cid], lb, 'too many dwarves')
                        }

                    elsif cnt < min
                        min += 1 if min < max and not LaborTool[lb]
                        (min-cnt).times {
                            c = @workers.sort_by { |_c|
                                malus = @worker_labor[_c.id].length * 10
                                malus += df.unit_entitypositions(_c.dfunit).length * 40
                                if sk = LaborSkill[lb] and usk = _c.dfunit.status.current_soul.skills.find { |_usk| _usk.id == sk }
                                    malus -= DFHack::SkillRating.int(usk.rating) * 4    # legendary => 15
                                end
                                [malus, rand]
                            }.find { |_c|
                                next if exclusive[_c.id]
                                next if LaborTool[lb] and @worker_labor[_c.id].find { |_lb| LaborTool[_lb] }
                                not @worker_labor[_c.id].include?(lb)
                            }

                            autolabor_setlabor(c, lb, 'not enough dwarves')
                        }

                    elsif not @idlers.empty?
                        more, desc = @labor_needmore[lb], 'idle'
                        more, desc = max - cnt, 'idleidle' if @labor_needmore.empty?
                        more.times do
                            break if @labor_worker[lb].length >= max
                            c = @idlers[rand(@idlers.length)]
                            autolabor_setlabor(c, lb, desc)
                        end
                    end
                }
            end
        end

        def autolabor_setlabor(c, lb, reason='no reason given')
            return if not c
            return if @worker_labor[c.id].include?(lb)
            @labor_worker[lb] << c.id
            @worker_labor[c.id] << lb
            u = c.dfunit
            if LaborTool[lb]
                LaborTool.keys.each { |_lb| u.status.labors[_lb] = false }
            end
            u.status.labors[lb] = true
            u.military.pickup_flags.update = true if LaborTool[lb]
            ai.debug "assigning labor #{lb} to #{DwarfAI::describe_unit(u)} (#{reason})"
        end

        def autolabor_unsetlabor(c, lb, reason='no reason given')
            return if not c
            return if not @worker_labor[c.id].include?(lb)
            u = c.dfunit
            return if LaborMedical[lb] and @medic[u.id]
            @labor_worker[lb].delete c.id
            @worker_labor[c.id].delete lb
            u.status.labors[lb] = false
            u.military.pickup_flags.update = true if LaborTool[lb]
            ai.debug "unassigning labor #{lb} from #{DwarfAI::describe_unit(u)} (#{reason})"
        end

        def set_up_trading(should_be_trading)
            return unless r = ai.plan.find_room(:workshop) { |_r| _r.subtype == :TradeDepot }
            return unless bld = r.dfbuilding
            return if bld.getBuildStage < bld.getMaxBuildStage
            return if bld.trade_flags.trader_requested == should_be_trading
            return unless view = df.curview and view._raw_rtti_classname == 'viewscreen_dwarfmodest'

            view.feed_keys(:D_BUILDJOB)
            df.center_viewscreen(r)
            view.feed_keys(:CURSOR_LEFT)
            view.feed_keys(:BUILDJOB_DEPOT_REQUEST_TRADER)
            view.feed_keys(:LEAVESCREEN)
        end

        def unit_hasmilitaryduty(u)
            return if u.military.squad_id == -1
            squad = df.world.squads.all.binsearch(u.military.squad_id)
            curmonth = squad.schedule[squad.cur_alert_idx][df.cur_year_tick / (1200*28)]
            !curmonth.orders.empty? and !(curmonth.orders.length == 1 and curmonth.orders[0].min_count == 0)
        end
    end
end
*/

// vim: et:sw=4:ts=4
