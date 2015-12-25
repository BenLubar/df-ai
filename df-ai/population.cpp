#include "ai.h"
#include "unit_entity_positions.h"

#include "modules/Gui.h"
#include "modules/Units.h"

#include "df/building.h"
#include "df/building_civzonest.h"
#include "df/building_farmplotst.h"
#include "df/building_stockpilest.h"
#include "df/building_tradedepotst.h"
#include "df/building_workshopst.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/entity_position_raw.h"
#include "df/entity_raw.h"
#include "df/general_ref.h"
#include "df/general_ref_building_civzone_assignedst.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/general_ref_unit_workerst.h"
#include "df/histfig_entity_link_positionst.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/interface_key.h"
#include "df/itemdef_weaponst.h"
#include "df/job.h"
#include "df/manager_order.h"
#include "df/reaction.h"
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
#include "df/viewscreen_dwarfmodest.h"
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
    workers(),
    idlers(),
    labor_needmore(),
    medic(),
    labor_worker(),
    worker_labor(),
    last_idle_year(-1),
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

static int32_t score_for_draft(df::unit *unit)
{
    return unit_entity_positions(unit, total_xp(unit), [](int32_t score, df::entity_position *pos) -> int32_t { return score + 5000; });
}

static bool compare_for_draft(df::unit *a, df::unit *b)
{
    return score_for_draft(a) < score_for_draft(b);
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
    ai->plan.attribute_noble_rooms(out, nobles);
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
    std::sort(maydraft.begin(), maydraft.end(), compare_for_draft);

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
                        auto pit = ai->plan.find_room(Plan::room_type::pit_cage, [cage](Plan::room *room) -> bool { df::coord room_pos = room->pos(); return room->building_id != -1 && abs(room_pos.x - cage->pos.x) <= 1 && abs(room_pos.y - cage->pos.y) <= 1 && room_pos.z == cage->pos.z; });
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

static const struct LaborInfo
{
    std::vector<df::unit_labor> list;
    std::set<df::unit_labor> tool;
    std::map<df::unit_labor, df::job_skill> skill;
    std::set<df::unit_labor> idle;
    std::set<df::unit_labor> medical;
    std::map<df::unit_labor, std::set<Stocks::good>> stocks;
    std::set<df::unit_labor> hauling;
    std::map<df::unit_labor, uint32_t> min;
    std::map<df::unit_labor, uint32_t> max;
    std::map<df::unit_labor, uint32_t> min_pct;
    std::map<df::unit_labor, uint32_t> max_pct;

    LaborInfo() {
        FOR_ENUM_ITEMS(unit_labor, l)
        {
            if (l != unit_labor::NONE)
            {
                list.push_back(l);

                min[l] = 2;
                max[l] = 8;
                min_pct[l] = 0;
                max_pct[l] = 0;

                skill[l] = job_skill::NONE;
            }
        }

        tool.insert(unit_labor::MINE);
        tool.insert(unit_labor::CUTWOOD);
        tool.insert(unit_labor::HUNT);

        FOR_ENUM_ITEMS(job_skill, s)
        {
            skill[ENUM_ATTR(job_skill, labor, s)] = s;
        }

        idle.insert(unit_labor::PLANT);
        idle.insert(unit_labor::HERBALIST);
        idle.insert(unit_labor::FISH);
        idle.insert(unit_labor::DETAIL);

        medical.insert(unit_labor::DIAGNOSE);
        medical.insert(unit_labor::SURGERY);
        medical.insert(unit_labor::BONE_SETTING);
        medical.insert(unit_labor::SUTURING);
        medical.insert(unit_labor::DRESSING_WOUNDS);
        medical.insert(unit_labor::FEED_WATER_CIVILIANS);

        stocks[unit_labor::PLANT].insert(Stocks::good::food);
        stocks[unit_labor::PLANT].insert(Stocks::good::drink);
        stocks[unit_labor::PLANT].insert(Stocks::good::cloth);
        stocks[unit_labor::HERBALIST].insert(Stocks::good::food);
        stocks[unit_labor::HERBALIST].insert(Stocks::good::drink);
        stocks[unit_labor::HERBALIST].insert(Stocks::good::cloth);
        stocks[unit_labor::FISH].insert(Stocks::good::food);

        hauling.insert(unit_labor::FEED_WATER_CIVILIANS);
        hauling.insert(unit_labor::RECOVER_WOUNDED);
        hauling.insert(unit_labor::HAUL_STONE);
        hauling.insert(unit_labor::HAUL_WOOD);
        hauling.insert(unit_labor::HAUL_BODY);
        hauling.insert(unit_labor::HAUL_FOOD);
        hauling.insert(unit_labor::HAUL_REFUSE);
        hauling.insert(unit_labor::HAUL_ITEM);
        hauling.insert(unit_labor::HAUL_FURNITURE);
        hauling.insert(unit_labor::HAUL_ANIMALS);
        hauling.insert(unit_labor::HAUL_TRADE);
        hauling.insert(unit_labor::HAUL_WATER);

        min[unit_labor::DETAIL] = 4;
        min[unit_labor::PLANT] = 4;
        min[unit_labor::HERBALIST] = 1;

        max[unit_labor::FISH] = 1;

        min_pct[unit_labor::DETAIL] = 5;
        min_pct[unit_labor::PLANT] = 30;
        min_pct[unit_labor::FISH] = 1;
        min_pct[unit_labor::HERBALIST] = 10;

        max_pct[unit_labor::DETAIL] = 20;
        max_pct[unit_labor::PLANT] = 60;
        max_pct[unit_labor::FISH] = 10;
        max_pct[unit_labor::HERBALIST] = 30;

        for (df::unit_labor l : hauling)
        {
            min_pct[l] = 30;
            max_pct[l] = 100;
        }
    }
} labors;


void Population::autolabors_workers(color_ostream & out)
{
    workers.clear();
    idlers.clear();
    labor_needmore.clear();
    std::map<df::unit *, std::string> nonworkers;
    medic.clear();
    for (auto assignment : ui->main.fortress_entity->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT])
    {
        df::historical_figure *hf = df::historical_figure::find(assignment->histfig);
        medic.insert(hf->unit_id);
    }

    bool merchant = false;
    for (df::unit *unit : world->units.active)
    {
        if (unit->flags1.bits.merchant && !unit->flags1.bits.dead)
        {
            merchant = true;
            break;
        }
    }
    set_up_trading(merchant);

    for (int32_t id : citizens)
    {
        df::unit *unit = df::unit::find(id);
        if (!unit)
            continue;
        if (unit->mood != mood_type::None)
        {
            nonworkers[unit] = "has mood: " + ENUM_KEY_STR(mood_type, unit->mood);
        }
        else if (Units::isChild(unit))
        {
            nonworkers[unit] = "is a child";
        }
        else if (has_military_duty(unit))
        {
            nonworkers[unit] = "has military duty";
        }
        else if (unit->flags1.bits.caged)
        {
            nonworkers[unit] = "caged";
        }
        else if (citizens.size() >= 20 &&
                !world->manager_orders.empty() &&
                !world->manager_orders.back()->is_validated &&
                unit_entity_positions(unit, false, [](bool exists, df::entity_position *pos) -> bool { return exists || pos->responsibilities[entity_position_responsibility::MANAGE_PRODUCTION]; }))
        {
            nonworkers[unit] = "validating work orders";
        }
        else if (merchant && unit_entity_positions(unit, false, [](bool exists, df::entity_position *pos) -> bool { return exists || pos->responsibilities[entity_position_responsibility::TRADE]; }))
        {
            nonworkers[unit] = "trading";
        }
        else if (unit->job.current_job && unit->job.current_job->job_type == job_type::Rest)
        {
            nonworkers[unit] = "resting injury";
        }
        else
        {
            bool has_activity = false;
            for (auto ref : unit->specific_refs)
            {
                if (ref->type == specific_ref_type::ACTIVITY)
                {
                    has_activity = true;
                    break;
                }
            }

            if (has_activity)
            {
                nonworkers[unit] = "has activity";
            }
            else
            {
                workers.insert(unit->id);
                if (!unit->job.current_job)
                {
                    idlers.insert(unit->id);
                }
            }
        }
    }

    // free non-workers
    for (auto entry : nonworkers)
    {
        df::unit *unit = entry.first;
        for (auto lb : labors.list)
        {
            if (labors.hauling.count(lb))
            {
                if (!unit->status.labors[lb] && !Units::isChild(unit))
                {
                    unit->status.labors[lb] = true;
                    ai->debug(out, "[pop] assigning labor " + ENUM_KEY_STR(unit_labor, lb) + " to " + ai->describe_unit(unit) + " (non-worker: " + entry.second + ")");
                }
            }
            else if (unit->status.labors[lb])
            {
                if (labors.medical.count(lb) && medic.count(unit->id))
                    continue;
                unit->status.labors[lb] = false;
                if (labors.tool.count(lb))
                {
                    unit->military.pickup_flags.bits.update = 1;
                }
                ai->debug(out, "[pop] unassigning labor " + ENUM_KEY_STR(unit_labor, lb) + " from " + ai->describe_unit(unit) + " (non-worker: " + entry.second + ")");
            }
        }
    }
}

void Population::autolabors_jobs(color_ostream & out)
{
    std::set<int32_t> seen_workshop;

    for (df::job_list_link *link = world->job_list.next; link; link = link->next)
    {
        df::general_ref_building_holderst *ref_bld = nullptr;
        df::general_ref_unit_workerst *ref_wrk = nullptr;

        for (auto ref : link->item->general_refs)
        {
            if (!ref_bld)
            {
                ref_bld = virtual_cast<df::general_ref_building_holderst>(ref);
            }
            if (!ref_wrk)
            {
                ref_wrk = virtual_cast<df::general_ref_unit_workerst>(ref);
            }

            if (ref_bld && ref_wrk)
                break;
        }

        if (ref_bld && seen_workshop.count(ref_bld->building_id))
            continue;

        if (!ref_wrk)
        {
            df::unit_labor job_labor = ENUM_ATTR(job_type, labor, link->item->job_type);
            if (job_labor == unit_labor::NONE)
                job_labor = ENUM_ATTR(job_skill, labor, ENUM_ATTR(job_type, skill, link->item->job_type));

            if (job_labor != unit_labor::NONE)
            {
                labor_needmore[job_labor]++;
            }
            else
            {
                switch (link->item->job_type)
                {
                    case job_type::ConstructBuilding:
                    case job_type::DestroyBuilding:
                        // TODO
                        break;
                    case job_type::CustomReaction:
                        for (auto reaction : world->raws.reactions)
                        {
                            if (reaction->code == link->item->reaction_name)
                            {
                                df::unit_labor job_labor = ENUM_ATTR(job_skill, labor, reaction->skill);
                                if (job_labor != unit_labor::NONE)
                                    labor_needmore[job_labor]++;
                                break;
                            }
                        }
                        break;
                    case job_type::PenSmallAnimal:
                    case job_type::PenLargeAnimal:
                        labor_needmore[unit_labor::HAUL_ANIMALS]++;
                        break;
                    case job_type::StoreOwnedItem:
                    case job_type::PlaceItemInTomb:
                    case job_type::StoreItemInStockpile:
                    case job_type::StoreItemInBag:
                    case job_type::StoreItemInHospital:
                    case job_type::StoreWeapon:
                    case job_type::StoreArmor:
                    case job_type::StoreItemInBarrel:
                    case job_type::StoreItemInBin:
                    case job_type::StoreItemInVehicle:
                        for (auto lb : labors.hauling)
                        {
                            labor_needmore[lb]++;
                        }
                        break;
                    default:
                        if (link->item->material_category.bits.wood)
                        {
                            labor_needmore[unit_labor::CARPENTER]++;
                        }
                        else if (link->item->material_category.bits.bone ||
                                link->item->material_category.bits.shell)
                        {
                            labor_needmore[unit_labor::BONE_CARVE]++;
                        }
                        else if (link->item->material_category.bits.cloth)
                        {
                            labor_needmore[unit_labor::CLOTHESMAKER]++;
                        }
                        else if (link->item->material_category.bits.leather)
                        {
                            labor_needmore[unit_labor::LEATHER]++;
                        }
                        else if (link->item->mat_type == 0)
                        {
                            // XXX metalcraft ?
                            labor_needmore[unit_labor::MASON]++;
                        }
                        else
                        {
                            ai->debug(out, "unknown labor for " + ENUM_KEY_STR(job_type, link->item->job_type));
                        }
                }
            }
        }

        if (ref_bld)
        {
            df::building *building = df::building::find(ref_bld->building_id);
            if (virtual_cast<df::building_farmplotst>(building) ||
                    virtual_cast<df::building_stockpilest>(building))
            {
                // parallel work allowed
            }
            else
            {
                seen_workshop.insert(ref_bld->building_id);
            }
        }
    }
}

void Population::autolabors_labors(color_ostream & out)
{
    // count active labors
    labor_worker.clear();
    worker_labor.clear();
    for (int32_t id : workers)
    {
        df::unit *unit = df::unit::find(id);
        for (df::unit_labor lb : labors.list)
        {
            if (unit->status.labors[lb])
            {
                labor_worker[lb].insert(id);
                worker_labor[id].insert(lb);
            }
        }
    }

    if (workers.size() > 15)
    {
        // if one has too many labors, free him up (one per round)
        size_t limit = 4 * labors.list.size() / workers.size();
        if (limit < 4)
            limit = 4;
        limit += labors.hauling.size();

        for (auto & entry : worker_labor)
        {
            if (entry.second.size() > limit && (!medic.count(entry.first) || entry.second.size() > limit + labors.medical.size()))
            {
                df::unit *unit = df::unit::find(entry.first);
                for (auto it = entry.second.begin(); it != entry.second.end(); )
                {
                    if (labors.medical.count(*it) && medic.count(entry.first))
                    {
                        it++;
                        continue;
                    }
                    labor_worker[*it].erase(entry.first);
                    unit->status.labors[*it] = false;
                    if (labors.tool.count(*it))
                        unit->military.pickup_flags.bits.update = 1;
                    entry.second.erase(it++);
                }
                ai->debug(out, "[pop] unassigned all labors from " + ai->describe_unit(unit) + " (too many labors)");
                break;
            }
        }
    }
}

void Population::autolabors_commit(color_ostream & out)
{
    // make copies
    std::map<df::unit_labor, uint32_t> labormin = labors.min;
    std::map<df::unit_labor, uint32_t> labormax = labors.max;
    std::map<df::unit_labor, uint32_t> laborminpct = labors.min_pct;
    std::map<df::unit_labor, uint32_t> labormaxpct = labors.max_pct;

    for (df::unit_labor lb : labors.list)
    {
        uint32_t max = labormax.at(lb);
        uint32_t maxpc = labormaxpct.at(lb) * workers.size() / 100;
        if (maxpc > max)
            max = maxpc;
        if (labors.stocks.count(lb))
        {
            bool need_any = false;
            for (auto good : labors.stocks.at(lb))
            {
                if (ai->stocks.need_more(good))
                {
                    need_any = true;
                    break;
                }
            }
            if (!need_any)
                max = 0;
        }

        if (labor_worker.count(lb) && labor_worker.at(lb).size() >= max)
        {
            labor_needmore.erase(lb);
        }
    }

    if (labor_needmore.empty() && !idlers.empty() && ai->plan.past_initial_phase() && last_idle_year != *cur_year)
    {
        ai->plan.idleidle(out);
        last_idle_year = *cur_year;
    }

    // handle low-number of workers + tool labors
    uint32_t mintool = 0;
    for (df::unit_labor lb : labors.tool)
    {
        uint32_t min = labormin.at(lb);
        uint32_t minpc = laborminpct.at(lb) * workers.size() / 100;
        if (minpc > min)
            min = minpc;
        mintool += min;
    }

    if (workers.size() < mintool)
    {
        for (df::unit_labor lb : labors.tool)
        {
            labormax[lb] = 0;
        }
        switch (workers.size())
        {
            case 0:
                // meh
                break;

            case 1:
                // switch mine or cutwood based on time (1/2 dwarf month each)
                if ((*cur_year_tick / 28 / (1200 / 2)) % 2 == 0)
                    labormax[unit_labor::MINE] = 1;
                else
                    labormax[unit_labor::CUTWOOD] = 1;
                break;

            default:
                // Divide workers equally between labors, with priority to
                // mining, then woodcutting, then hunting.
                // XXX new labors.tool?
                uint32_t base = workers.size() / 3;
                labormax[unit_labor::MINE] = base + (workers.size() % 3 > 0 ? 1 : 0);
                labormax[unit_labor::CUTWOOD] = base + (workers.size() % 3 > 1 ? 1 : 0);
                labormax[unit_labor::HUNT] = base;
                break;
        }
    }

    // list of dwarves with an exclusive labor
    std::map<int32_t, df::unit_labor> exclusive;
#define EXCLUSIVE_LABOR(lb, condition) \
    if (workers.size() > exclusive.size() + 2 && labor_needmore.count(unit_labor::lb) && labor_needmore.at(unit_labor::lb) && (condition)) \
    { \
        if (labor_worker.count(unit_labor::lb)) \
        { \
            /* keep last run's choice */ \
            int32_t id = -1; \
            for (int32_t uid : labor_worker.at(unit_labor::lb)) \
            { \
                if (id == -1 || worker_labor.at(uid).size() < worker_labor.at(id).size()) \
                    id = uid; \
            } \
            if (id != -1) \
            { \
                exclusive[id] = unit_labor::lb; \
                idlers.erase(id); \
                df::unit *unit = df::unit::find(id); \
                std::set<df::unit_labor> labors_copy = worker_labor[id]; \
                for (df::unit_labor other : labors_copy) \
                { \
                    if (unit_labor::lb == other) \
                        continue; \
                    if (labors.tool.count(other)) \
                        continue; \
                    unassign_labor(out, unit, other, "has exclusive labor: " + ENUM_KEY_STR(unit_labor, unit_labor::lb)); \
                } \
            } \
        } \
    }
#define WORKSHOP_WITH_JOBS(t) \
    ai->plan.find_room(Plan::room_type::workshop, [](Plan::room *r) -> bool { \
                if (r->info.workshop.type != Plan::workshop_type::t) \
                    return false; \
                df::building_workshopst *building = virtual_cast<df::building_workshopst>(df::building::find(r->building_id)); \
                return building && !building->jobs.empty(); \
            })

    EXCLUSIVE_LABOR(CARPENTER, WORKSHOP_WITH_JOBS(Carpenters));
    EXCLUSIVE_LABOR(MINE, ai->plan.is_digging());
    EXCLUSIVE_LABOR(MASON, WORKSHOP_WITH_JOBS(Masons));
    EXCLUSIVE_LABOR(CUTWOOD, ai->stocks.is_cutting_trees());
    EXCLUSIVE_LABOR(DETAIL, ai->plan.find_room(Plan::room_type::cistern_well, [](Plan::room *r) -> bool { return !r->info.cistern_well.channeled; }));

#undef WORKSHOP_WITH_JOBS
#undef EXCLUSIVE_LABOR

    // autolabor!
    for (df::unit_labor lb : labors.list)
    {
        uint32_t min = labormin.at(lb);
        uint32_t max = labormax.at(lb);
        uint32_t minpc = laborminpct.at(lb) * workers.size() / 100;
        uint32_t maxpc = labormaxpct.at(lb) * workers.size() / 100;
        if (minpc > min)
            min = minpc;
        if (maxpc > max)
            max = maxpc;
        if (labor_needmore.empty() && labors.idle.count(lb))
            max = workers.size();
        if (lb == unit_labor::FISH && !ai->plan.find_room(Plan::room_type::workshop, [](Plan::room *r) -> bool { return r->info.workshop.type == Plan::workshop_type::Fishery && r->status != Plan::room_status::plan; }))
            max = 0;
        if (labors.stocks.count(lb))
        {
            bool need_any = false;
            for (auto good : labors.stocks.at(lb))
            {
                if (ai->stocks.need_more(good))
                {
                    need_any = true;
                    break;
                }
            }
            if (!need_any)
                max = 0;
        }
        if (min > max)
            min = max;
        if (min > workers.size())
            min = workers.size();

        uint32_t count = labor_worker.count(lb) ? labor_worker.at(lb).size() : 0;
        if (count > max)
        {
            if (labor_needmore.empty())
                continue;
            df::job_skill skill = labors.skill.at(lb);
            for (uint32_t i = max; i < count; i++)
            {
                df::unit *worst = nullptr;
                int32_t worst_skill = ((int32_t) skill_rating::Legendary5) + 1;
                for (int32_t id : labor_worker[lb])
                {
                    df::unit *unit = df::unit::find(id);
                    if (skill == job_skill::NONE)
                    {
                        worst = unit;
                        break;
                    }
                    bool has_skill = false;
                    for (auto sk : unit->status.current_soul->skills)
                    {
                        if (sk->id == skill)
                        {
                            has_skill = true;
                            if (worst_skill > sk->rating)
                            {
                                worst = unit;
                                worst_skill = sk->rating;
                            }
                            break;
                        }
                    }
                    if (!has_skill)
                    {
                        worst = unit;
                        break;
                    }
                }
                unassign_labor(out, worst, lb, "too many dwarves");
            }
        }
        else if (count < min)
        {
            if (min < max && !labors.tool.count(lb))
                min++;

            df::job_skill skill = labors.skill.at(lb);

            for (uint32_t i = count; i < min; i++)
            {
                df::unit *best = nullptr;
                int best_score = 0;
                for (int32_t id : workers)
                {
                    if (worker_labor.count(id) && worker_labor.at(id).count(lb))
                        continue;
                    if (exclusive.count(id))
                        continue;
                    if (labors.tool.count(lb) && worker_labor.count(id))
                    {
                        bool any_tool = false;
                        for (df::unit_labor tool : labors.tool)
                        {
                            if (worker_labor.at(id).count(tool))
                            {
                                any_tool = true;
                                break;
                            }
                        }

                        if (any_tool)
                            continue;
                    }

                    df::unit *unit = df::unit::find(id);
                    int score = unit_entity_positions(unit, worker_labor.count(id) ? worker_labor.at(id).size() * 10 : 0, [](int n, df::entity_position *pos) -> int { return n + 40; });
                    if (skill != job_skill::NONE)
                    {
                        for (auto sk : unit->status.current_soul->skills)
                        {
                            if (sk->id == skill)
                            {
                                score -= ((int) sk->rating) * 4;
                                break;
                            }
                        }
                    }

                    if (score < best_score)
                    {
                        best = unit;
                        best_score = score;
                    }
                }

                assign_labor(out, best, lb, "not enough dwarves");
            }
        }
        else if (!idlers.empty())
        {
            uint32_t more = max - count;
            std::string desc = "idleidle";
            if (!labor_needmore.empty())
            {
                more = labor_needmore.count(lb) ? labor_needmore.at(lb) : 0;
                desc = "idle";
            }

            for (uint32_t i = 0; i < more; i++)
            {
                if (labor_worker[lb].size() >= max)
                    break;

                auto id = idlers.begin();
                std::advance(id, std::uniform_int_distribution<size_t>(0, idlers.size() - 1)(ai->rng));

                assign_labor(out, df::unit::find(*id), lb, desc);
            }
        }
    }
}

void Population::assign_labor(color_ostream & out, df::unit *unit, df::unit_labor labor, std::string reason)
{
    if (worker_labor.count(unit->id) && worker_labor.at(unit->id).count(labor))
        return;
    labor_worker[labor].insert(unit->id);
    worker_labor[unit->id].insert(labor);
    if (labors.tool.count(labor))
    {
        for (auto lb : labors.tool)
        {
            unassign_labor(out, unit, lb, "conflicts with " + ENUM_KEY_STR(unit_labor, labor) + " - " + reason);
        }
        unit->military.pickup_flags.bits.update = 1;
    }
    unit->status.labors[labor] = true;
    ai->debug(out, "[pop] assigning labor " + ENUM_KEY_STR(unit_labor, labor) + " to " + ai->describe_unit(unit) + " (" + reason + ")");
}

void Population::unassign_labor(color_ostream & out, df::unit *unit, df::unit_labor labor, std::string reason)
{
    if (!worker_labor.count(unit->id) || !worker_labor.at(unit->id).count(labor))
        return;
    if (labors.medical.count(labor) && medic.count(unit->id))
        return;
    labor_worker[labor].erase(unit->id);
    worker_labor[unit->id].erase(labor);
    if (labors.tool.count(labor))
    {
        unit->military.pickup_flags.bits.update = 1;
    }
    unit->status.labors[labor] = false;
    ai->debug(out, "[pop] unassigning labor " + ENUM_KEY_STR(unit_labor, labor) + " from " + ai->describe_unit(unit) + " (" + reason + ")");
}

void Population::set_up_trading(bool should_be_trading)
{
    Plan::room *room = ai->plan.find_room(Plan::room_type::workshop, [](Plan::room *r) -> bool { return r->info.workshop.type == Plan::workshop_type::TradeDepot; });
    if (!room)
        return;
    df::building_tradedepotst *depot = virtual_cast<df::building_tradedepotst>(df::building::find(room->building_id));
    if (!depot || depot->getBuildStage() < depot->getMaxBuildStage())
        return;
    if (depot->trade_flags.bits.trader_requested == should_be_trading)
        return;
    if (!strict_virtual_cast<df::viewscreen_dwarfmodest>(Gui::getCurViewscreen()))
        return;

    std::set<df::interface_key> keys;
#define KEY(key) \
            keys.clear(); \
            keys.insert(interface_key::key); \
            Gui::getCurViewscreen()->feed(&keys)
    KEY(D_BUILDJOB);
    df::coord pos = room->pos();
    Gui::setCursorCoords(pos.x, pos.y, pos.z);
    KEY(CURSOR_LEFT);
    KEY(BUILDJOB_DEPOT_REQUEST_TRADER);
    KEY(LEAVESCREEN);
#undef KEY
}

bool Population::has_military_duty(df::unit *unit)
{
    if (unit->military.squad_id == -1)
        return false;

    df::squad *squad = df::squad::find(unit->military.squad_id);
    const auto & curmonth = squad->schedule[squad->cur_alert_idx][*cur_year_tick / 28 / 1200];

    return !curmonth->orders.empty() && (curmonth->orders.size() != 1 || curmonth->orders[0]->min_count != 0);
}

// vim: et:sw=4:ts=4
