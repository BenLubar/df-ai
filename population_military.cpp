#include "ai.h"
#include "exclusive_callback.h"
#include "population.h"
#include "plan.h"

#include "modules/Gui.h"
#include "modules/Units.h"

#include "df/entity_material_category.h"
#include "df/entity_position_assignment.h"
#include "df/entity_uniform.h"
#include "df/entity_uniform_item.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/item_weaponst.h"
#include "df/itemdef_weaponst.h"
#include "df/layer_object_listst.h"
#include "df/occupation.h"
#include "df/squad.h"
#include "df/squad_ammo_spec.h"
#include "df/squad_order_kill_listst.h"
#include "df/squad_position.h"
#include "df/squad_uniform_spec.h"
#include "df/ui.h"
#include "df/uniform_category.h"
#include "df/viewscreen_layer_militaryst.h"
#include "df/world.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

class MilitarySetupExclusive : public ExclusiveCallback
{
    AI * const ai;
    std::list<int32_t> units;

protected:
    typedef typename std::vector<df::unit *>::const_iterator unit_iterator;
    MilitarySetupExclusive(AI * const ai, const std::string & description, unit_iterator begin, unit_iterator end) : ExclusiveCallback(description, 2), ai(ai)
    {
        for (auto it = begin; it != end; it++)
        {
            units.push_back((*it)->id);
        }
    }
    df::viewscreen_layer_militaryst *getScreen() const
    {
        return virtual_cast<df::viewscreen_layer_militaryst>(Gui::getCurViewscreen(false));
    }
    template<typename T>
    T *getActiveObject() const
    {
        auto screen = getScreen();
        if (!screen)
        {
            return nullptr;
        }

        for (auto obj : screen->layer_objects)
        {
            if (obj->enabled && obj->active)
            {
                return virtual_cast<T>(obj);
            }
        }

        return nullptr;
    }
    virtual void Run(color_ostream & out)
    {
        Key(interface_key::D_MILITARY);

        while (!units.empty())
        {
            AssertDelayed();

            auto screen = getScreen();
            if (!screen)
            {
                ai->debug(out, "[ERROR] unable to open military screen");
                units.pop_front();
                break;
            }

            Run(out, screen, units.front());

            units.pop_front();
        }

        Key(interface_key::LEAVESCREEN);
    }
    virtual void Run(color_ostream & out, df::viewscreen_layer_militaryst *screen, int32_t unit_id) = 0;
    void ScrollTo(int32_t index)
    {
        auto list = getActiveObject<df::layer_object_listst>();
        if (!list || index < 0 || index >= list->num_entries)
        {
            return;
        }

        while (list->getFirstVisible() > index)
        {
            Key(interface_key::STANDARDSCROLL_PAGEUP);
        }

        while (list->getLastVisible() < index)
        {
            Key(interface_key::STANDARDSCROLL_PAGEUP);
        }

        MoveToItem(&list->cursor, index);
    }

public:
    class Draft;
    class Dismiss;
};

class MilitarySetupExclusive::Draft : public MilitarySetupExclusive
{
public:
    Draft(AI * const ai, unit_iterator begin, unit_iterator end) : MilitarySetupExclusive(ai, "military - draft", begin, end)
    {
    }

    df::squad *find_good_squad(df::viewscreen_layer_militaryst *screen, int32_t squad_size)
    {
        for (auto squad : screen->squads.list)
        {
            if (squad && std::count_if(squad->positions.begin(), squad->positions.end(), [](df::squad_position *pos) -> bool { return pos && pos->occupant != -1; }) < squad_size)
            {
                return squad;
            }
        }

        return nullptr;
    }

    virtual void Run(color_ostream & out, df::viewscreen_layer_militaryst *screen, int32_t unit_id)
    {
        int32_t squad_size = 10;
        int32_t num_soldiers = screen->num_soldiers + int32_t(units.size());
        if (num_soldiers < 4 * 8)
            squad_size = 8;
        if (num_soldiers < 4 * 6)
            squad_size = 6;
        if (num_soldiers < 3 * 4)
            squad_size = 4;

        df::squad *selected_squad = find_good_squad(screen, squad_size);

        if (!selected_squad)
        {
            ai->debug(out, "Creating new squad...");

            int32_t selected_uniform = SelectOrCreateUniform(out, screen->squads.list.size());

            auto vacant_squad_slot = std::find(screen->squads.list.begin(), screen->squads.list.end(), static_cast<df::squad *>(nullptr));

            if (vacant_squad_slot != screen->squads.list.end())
            {
                ScrollTo(int32_t(vacant_squad_slot - screen->squads.list.begin()));

                Key(interface_key::D_MILITARY_CREATE_SQUAD);
            }
            else
            {
                auto squad_with_leader = std::find(screen->squads.can_appoint.begin(), screen->squads.can_appoint.end(), true);

                ScrollTo(int32_t(squad_with_leader - screen->squads.can_appoint.begin()));

                Key(interface_key::D_MILITARY_CREATE_SUB_SQUAD);
            }

            ScrollTo(selected_uniform);

            Key(interface_key::SELECT);

            selected_squad = find_good_squad(screen, squad_size);
            CHECK_NULL_POINTER(selected_squad);
        }

        ai->debug(out, "Selecting squad: " + AI::describe_name(selected_squad->name, true));

        ScrollTo(linear_index(screen->squads.list, selected_squad));

        bool should_reassign_leader = true;
        if (!ai->pop->resident.count(unit_id))
        {
            // Mercenaries can't lead squads.
            should_reassign_leader = false;
        }
        else
        {
            for (auto pos : selected_squad->positions)
            {
                if (pos)
                {
                    if (auto fig = df::historical_figure::find(pos->occupant))
                    {
                        if (auto u = df::unit::find(fig->unit_id))
                        {
                            if (Units::isAlive(u) && Units::isSane(u))
                            {
                                // Squad already has a leader.
                                should_reassign_leader = false;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (should_reassign_leader)
        {
            // Move an existing citizen soldier into this squad as the leader.
            for (auto squad : screen->squads.list)
            {
                if (!squad)
                {
                    continue;
                }

                for (auto pos : squad->positions)
                {
                    if (!pos)
                    {
                        continue;
                    }

                    if (auto fig = df::historical_figure::find(pos->occupant))
                    {
                        if (ai->pop->resident.count(fig->unit_id))
                        {
                            continue;
                        }

                        if (auto u = df::unit::find(fig->unit_id))
                        {
                            if (squad->leader_position == u->military.squad_position)
                            {
                                continue;
                            }

                            ai->debug(out, "Reassigning " + AI::describe_unit(u) + " from " + AI::describe_name(squad->name, true) + " to lead new squad " + AI::describe_name(selected_squad->name, true));
                            Key(interface_key::STANDARDSCROLL_LEFT);

                            auto selected_unit = std::find(screen->positions.candidates.begin(), screen->positions.candidates.end(), u);

                            ScrollTo(int32_t(selected_unit - screen->positions.candidates.begin()));

                            Key(interface_key::SELECT);

                            Key(interface_key::STANDARDSCROLL_RIGHT);

                            should_reassign_leader = false;

                            break;
                        }
                    }
                }

                if (!should_reassign_leader)
                {
                    break;
                }
            }

            if (should_reassign_leader)
            {
                ai->debug(out, "[WARNING] Could not find an existing soldier to lead squad: " + AI::describe_name(selected_squad->name, true));
            }
        }

        Key(interface_key::STANDARDSCROLL_RIGHT);

        df::layer_object_listst *list;
        while ((list = getActiveObject<df::layer_object_listst>()) != nullptr && screen->positions.assigned.at(size_t(list->cursor)))
        {
            Key(interface_key::STANDARDSCROLL_DOWN);
        }

        Key(interface_key::STANDARDSCROLL_RIGHT);

        auto selected_unit = std::find_if(screen->positions.candidates.begin(), screen->positions.candidates.end(), [&](df::unit *u) -> bool
        {
            return u->id == unit_id;
        });

        if (selected_unit == screen->positions.candidates.end())
        {
            ai->debug(out, "Failed to recruit " + AI::describe_unit(df::unit::find(unit_id)) + ": could not find unit on list of candidates");
        }
        else
        {
            ai->debug(out, "Recruiting new squad member: " + AI::describe_unit(df::unit::find(unit_id)));

            ScrollTo(int32_t(selected_unit - screen->positions.candidates.begin()));

            Key(interface_key::SELECT);
        }

        Key(interface_key::STANDARDSCROLL_LEFT);

        Key(interface_key::STANDARDSCROLL_LEFT);
    }

    int32_t SelectOrCreateUniform(color_ostream & out, size_t existing_squad_count)
    {
        bool ranged = existing_squad_count % 3 == 0;

        df::uniform_indiv_choice indiv_choice;
        if (ranged)
        {
            indiv_choice.bits.ranged = 1;
        }
        else
        {
            indiv_choice.bits.melee = 1;
        }

        auto check_uniform_item = [](df::entity_uniform *u, df::uniform_category cat, df::item_type item_type, df::entity_material_category mat_class = entity_material_category::Armor, df::uniform_indiv_choice indiv_choice = df::uniform_indiv_choice()) -> bool
        {
            return u->uniform_item_types[cat].size() == 1 && u->uniform_item_types[cat].at(0) == item_type && u->uniform_item_info[cat].at(0)->material_class == mat_class && u->uniform_item_info[cat].at(0)->indiv_choice.whole == indiv_choice.whole;
        };

        auto selected = std::find_if(ui->main.fortress_entity->uniforms.begin(), ui->main.fortress_entity->uniforms.end(), [&](df::entity_uniform *u) -> bool
        {
            return u->flags.bits.exact_matches && !u->flags.bits.replace_clothing &&
                check_uniform_item(u, uniform_category::body, item_type::ARMOR) &&
                check_uniform_item(u, uniform_category::head, item_type::HELM) &&
                check_uniform_item(u, uniform_category::pants, item_type::PANTS) &&
                check_uniform_item(u, uniform_category::gloves, item_type::GLOVES) &&
                check_uniform_item(u, uniform_category::shoes, item_type::SHOES) &&
                check_uniform_item(u, uniform_category::shield, item_type::SHIELD) &&
                check_uniform_item(u, uniform_category::weapon, item_type::WEAPON, entity_material_category::None, indiv_choice);
        });

        if (selected != ui->main.fortress_entity->uniforms.end())
        {
            ai->debug(out, "Selected uniform for squad: " + (*selected)->name);
            return int32_t(selected - ui->main.fortress_entity->uniforms.begin());
        }

        Key(interface_key::D_MILITARY_UNIFORMS);

        Key(interface_key::D_MILITARY_ADD_UNIFORM);

        Key(interface_key::D_MILITARY_NAME_UNIFORM);

        auto screen = getScreen();

        EnterString(&screen->equip.uniforms.back()->name, ranged ? "Heavy ranged" : "Heavy melee");

        Key(interface_key::SELECT);

        Key(interface_key::STANDARDSCROLL_LEFT);

        auto add_uniform_part = [&](df::interface_key type)
        {
            Key(type);
            if (type == interface_key::D_MILITARY_ADD_WEAPON)
            {
                df::uniform_indiv_choice indiv_choice;
                if (ranged)
                {
                    indiv_choice.bits.ranged = 1;
                }
                else
                {
                    indiv_choice.bits.melee = 1;
                }
                auto selected_weapon = std::find_if(screen->equip.add_item.indiv_choice.begin(), screen->equip.add_item.indiv_choice.end(), [&](df::uniform_indiv_choice choice) -> bool
                {
                    return indiv_choice.whole == choice.whole;
                });
                ScrollTo(int32_t(selected_weapon - screen->equip.add_item.indiv_choice.begin()));
            }
            else
            {
                Key(interface_key::SELECT);
                Key(interface_key::D_MILITARY_ADD_MATERIAL);
                Key(interface_key::STANDARDSCROLL_LEFT);
                Key(interface_key::STANDARDSCROLL_UP);
                Key(interface_key::STANDARDSCROLL_RIGHT);
                auto armor_material = std::find(screen->equip.material.generic.begin(), screen->equip.material.generic.end(), entity_material_category::Armor);
                ScrollTo(int32_t(armor_material - screen->equip.material.generic.begin()));
            }
            Key(interface_key::SELECT);
        };

        add_uniform_part(interface_key::D_MILITARY_ADD_ARMOR);
        add_uniform_part(interface_key::D_MILITARY_ADD_PANTS);
        add_uniform_part(interface_key::D_MILITARY_ADD_HELM);
        add_uniform_part(interface_key::D_MILITARY_ADD_GLOVES);
        add_uniform_part(interface_key::D_MILITARY_ADD_BOOTS);
        add_uniform_part(interface_key::D_MILITARY_ADD_SHIELD);
        add_uniform_part(interface_key::D_MILITARY_ADD_WEAPON);

        if (!ui->main.fortress_entity->uniforms.back()->flags.bits.exact_matches)
        {
            Key(interface_key::D_MILITARY_EXACT_MATCH);
        }

        if (ui->main.fortress_entity->uniforms.back()->flags.bits.replace_clothing)
        {
            Key(interface_key::D_MILITARY_REPLACE_CLOTHING);
        }

        Key(interface_key::D_MILITARY_POSITIONS);

        ai->debug(out, ranged ? "Created new uniform for ranged squad." : "Created new uniform for melee squad.");

        return int32_t(ui->main.fortress_entity->uniforms.size() - 1);
    }

    static bool compare(const df::unit *a, const df::unit *b)
    {
        return Population::unit_totalxp(a) < Population::unit_totalxp(b);
    }
};

class MilitarySetupExclusive::Dismiss : public MilitarySetupExclusive
{
public:
    Dismiss(AI * const ai, unit_iterator begin, unit_iterator end) : MilitarySetupExclusive(ai, "military - dismiss", begin, end)
    {
    }

    virtual void Run(color_ostream & out, df::viewscreen_layer_militaryst *screen, int32_t unit_id)
    {
        auto u = df::unit::find(unit_id);
        if (!u)
        {
            return;
        }

        auto squad = df::squad::find(u->military.squad_id);
        auto squad_pos = std::find(screen->squads.list.begin(), screen->squads.list.end(), squad);
        if (!squad || squad_pos == screen->squads.list.end())
        {
            ai->debug(out, "[ERROR] Cannot find squad for unit: " + AI::describe_unit(u));
            return;
        }

        ScrollTo(int32_t(squad_pos - screen->squads.list.begin()));

        Key(interface_key::STANDARDSCROLL_RIGHT);

        auto position = std::find(screen->positions.assigned.begin(), screen->positions.assigned.end(), u);
        if (position == screen->positions.assigned.end())
        {
            ai->debug(out, "[ERROR] Cannot find unit in squad: " + AI::describe_unit(u));

            Key(interface_key::STANDARDSCROLL_LEFT);

            return;
        }

        ScrollTo(int32_t(position - screen->positions.assigned.begin()));

        Key(interface_key::SELECT);

        Key(interface_key::STANDARDSCROLL_LEFT);
    }

    static bool compare(const df::unit *a, const df::unit *b)
    {
        return Population::unit_totalxp(a) > Population::unit_totalxp(b);
    }
};

void Population::update_military(color_ostream & out)
{
    std::vector<df::unit *> soldiers;
    std::vector<df::unit *> want_draft;
    std::vector<df::unit *> draft_pool;

    for (auto u : world->units.active)
    {
        if (Units::isCitizen(u))
        {
            if (u->military.squad_id == -1)
            {
                if (military.erase(u->id))
                {
                    ai->plan->freesoldierbarrack(out, u->id);
                }
                std::vector<Units::NoblePosition> positions;
                if (!Units::isChild(u) && !Units::isBaby(u) && u->mood == mood_type::None && !Units::getNoblePositions(&positions, u) &&
                    !u->status.labors[unit_labor::MINE] && !u->status.labors[unit_labor::CUTWOOD] && !u->status.labors[unit_labor::HUNT])
                {
                    draft_pool.push_back(u);
                }
            }
            else
            {
                if (!military.count(u->id))
                {
                    military[u->id] = u->military.squad_id;
                    ai->plan->getsoldierbarrack(out, u->id);
                }

                auto squad = df::squad::find(u->military.squad_id);
                if (squad && (squad->leader_position != u->military.squad_position || std::find_if(squad->positions.begin(), squad->positions.end(), [u](df::squad_position *pos) -> bool
                {
                    if (!pos || pos->occupant == u->hist_figure_id)
                    {
                        return false;
                    }
                    auto occupant = df::historical_figure::find(pos->occupant);
                    if (!occupant)
                    {
                        return false;
                    }
                    auto occupant_unit = df::unit::find(occupant->unit_id);
                    return occupant_unit && Units::isAlive(occupant_unit);
                }) == squad->positions.end()))
                {
                    // Only allow removing squad leaders if they are the only one in the squad.
                    soldiers.push_back(u);
                }
            }
        }
        else if (resident.count(u->id))
        {
            if (u->military.squad_id == -1)
            {
                if (military.erase(u->id))
                {
                    ai->plan->freesoldierbarrack(out, u->id);
                }

                // Soldier residents should be recruited into the military.
                if (Units::isSane(u) && std::find_if(u->occupations.begin(), u->occupations.end(), [](df::occupation *occ) -> bool { return occ->type == occupation_type::MERCENARY; }) != u->occupations.end())
                {
                    want_draft.push_back(u);
                }
            }
            else if (!military.count(u->id))
            {
                military[u->id] = u->military.squad_id;
                ai->plan->getsoldierbarrack(out, u->id);
            }
        }
    }

    size_t axes = 0, picks = 0;
    for (const auto & st : ai->stocks->count_subtype[stock_item::axe])
    {
        axes += size_t(st.second.second);
    }
    for (const auto & st : ai->stocks->count_subtype[stock_item::pick])
    {
        picks += size_t(st.second.second);
    }

    size_t max_military = std::min(citizen.size() * 3 / 4, std::min(axes ? axes - 1 : 0, picks ? picks - 1 : 0));
    size_t min_military = std::min(citizen.size() / 4, max_military);
    size_t citizen_military = std::count_if(military.begin(), military.end(), [this](const std::pair<const int32_t, int32_t> & member) -> bool
    {
        return citizen.count(member.first);
    });

    if (citizen_military > max_military)
    {
        auto mid = soldiers.begin() + std::min(citizen_military - max_military, soldiers.size());
        std::partial_sort(soldiers.begin(), mid, soldiers.end(), &MilitarySetupExclusive::Dismiss::compare);
        events.queue_exclusive(new MilitarySetupExclusive::Dismiss(ai, soldiers.begin(), mid));
    }
    else if (citizen_military < min_military)
    {
        auto mid = draft_pool.begin() + std::min(max_military - citizen_military, draft_pool.size());
        std::partial_sort(draft_pool.begin(), mid, draft_pool.end(), &MilitarySetupExclusive::Draft::compare);
        want_draft.insert(want_draft.end(), draft_pool.begin(), mid);
    }

    if (!want_draft.empty())
    {
        events.queue_exclusive(new MilitarySetupExclusive::Draft(ai, want_draft.begin(), want_draft.end()));
    }

    // Check barracks construction status.
    ai->find_room(room_type::barracks, [](room *r) -> bool
    {
        if (r->status < room_status::dug)
        {
            return false;
        }

        auto squad = df::squad::find(r->squad_id);
        if (!squad)
        {
            return false;
        }

        auto training_equipment = df::building::find(r->bld_id);
        if (!training_equipment)
        {
            return false;
        }

        if (training_equipment->getBuildStage() < training_equipment->getMaxBuildStage())
        {
            return false;
        }

        // Barracks is ready to train soldiers. Send the squad to training.
        if (!squad->cur_alert_idx)
        {
            squad->cur_alert_idx = 1; // training
        }

        return false;
    });
}

class MilitarySquadAttackExclusive : public ExclusiveCallback
{
    AI * const ai;
    std::map<int32_t, std::set<int32_t>> kill_orders;
public:
    MilitarySquadAttackExclusive(AI * const ai) : ExclusiveCallback("squad attack updater", 2), ai(ai)
    {
    }

    virtual bool SuppressStateChange(color_ostream &, state_change_event event)
    {
        return event == SC_PAUSED || event == SC_UNPAUSED;
    }

    virtual void Run(color_ostream & out)
    {
        using change_type = Population::squad_order_change;

        kill_orders.clear();

        for (const auto & change : ai->pop->squad_order_changes)
        {
            auto squad = df::squad::find(change.squad_id);
            auto unit = df::unit::find(change.unit_id);
            if (!squad || !unit)
            {
                continue;
            }

            switch (change.type)
            {
            case change_type::kill:
                if (!kill_orders.count(squad->id))
                {
                    std::set<int32_t> units;

                    for (auto order : squad->orders)
                    {
                        if (auto kill = strict_virtual_cast<df::squad_order_kill_listst>(order))
                        {
                            units.insert(kill->units.begin(), kill->units.end());
                        }
                    }

                    if (change.remove == (units.count(unit->id) == 0))
                    {
                        // This won't cause anything to change, so don't set the squad's orders.
                        continue;
                    }

                    kill_orders[squad->id] = units;
                }

                if (change.remove)
                {
                    if (kill_orders.at(squad->id).erase(unit->id))
                    {
                        ai->debug(out, "Cancelling squad order for " + AI::describe_name(squad->name, true) + " to kill " + AI::describe_unit(unit) + ": " + change.reason);
                    }
                }
                else
                {
                    if (kill_orders.at(squad->id).insert(unit->id).second)
                    {
                        ai->debug(out, "Ordering squad " + AI::describe_name(squad->name, true) + " to kill " + AI::describe_unit(unit) + ": " + change.reason);
                    }
                }
                break;
            }
        }

        ai->pop->squad_order_changes.clear();

        if (!kill_orders.empty())
        {
            Key(interface_key::D_PAUSE);
            Key(interface_key::D_SQUADS);

            for (auto kill_orders_squad : kill_orders)
            {
                auto squad_pos = std::find_if(ui->squads.list.begin(), ui->squads.list.end(), [&](df::squad *squad) -> bool
                {
                    return squad->id == kill_orders_squad.first;
                }) - ui->squads.list.begin();
                auto first_squad = [&]() -> ptrdiff_t
                {
                    return ui->squads.list.size() > 9 ? std::find_if(ui->squads.list.begin(), ui->squads.list.end(), [](df::squad *squad)
                    {
                        return squad->id == ui->squads.unk48;
                    }) - ui->squads.list.begin() : 0;
                };

                // Menu is guaranteed to be capped at 10 squads: a, b, c, d, e, f, g, h, i, j. k is reserved for kill orders.
                while (first_squad() > squad_pos)
                {
                    Key(interface_key::SECONDSCROLL_PAGEUP);
                }
                while (first_squad() + 10 <= squad_pos)
                {
                    Key(interface_key::SECONDSCROLL_PAGEDOWN);
                }

                Key(static_cast<df::interface_key>(interface_key::OPTION1 + squad_pos - first_squad()));

                if (kill_orders_squad.second.empty())
                {
                    Key(interface_key::D_SQUADS_CANCEL_ORDER);
                    continue;
                }

                Key(interface_key::D_SQUADS_KILL);
                Key(interface_key::D_SQUADS_KILL_LIST);

                for (auto kill_orders_unit : kill_orders_squad.second)
                {
                    auto unit_it = std::find_if(ui->squads.kill_targets.begin(), ui->squads.kill_targets.end(), [&](df::unit *u) -> bool
                    {
                        return u->id == kill_orders_unit;
                    });
                    if (unit_it == ui->squads.kill_targets.end())
                    {
                        ai->debug(out, "[ERROR] Cannot find unit for squad kill order: " + AI::describe_unit(df::unit::find(kill_orders_unit)));
                        continue;
                    }

                    auto unit_pos = int32_t(unit_it - ui->squads.kill_targets.begin());
                    auto & first_unit = ui->squads.unk_f0;

                    while (first_unit > unit_pos)
                    {
                        Key(interface_key::SECONDSCROLL_PAGEUP);
                    }
                    while (first_unit + 10 <= unit_pos)
                    {
                        Key(interface_key::SECONDSCROLL_PAGEDOWN);
                    }

                    Key(static_cast<df::interface_key>(interface_key::SEC_OPTION1 + unit_pos - first_unit));
                }

                Key(interface_key::SELECT);

                if (ui->squads.in_kill_list)
                {
                    Key(interface_key::LEAVESCREEN);
                    Key(interface_key::LEAVESCREEN);
                }
            }

            Key(interface_key::LEAVESCREEN);
            Key(interface_key::D_PAUSE);
        }
    }
};

bool Population::military_random_squad_attack_unit(color_ostream & out, df::unit *u, const std::string & reason)
{
    df::squad *squad = nullptr;
    int32_t best = std::numeric_limits<int32_t>::min();
    for (auto sqid : ui->main.fortress_entity->squads)
    {
        df::squad *sq = df::squad::find(sqid);

        int32_t score = 0;
        for (auto sp : sq->positions)
        {
            if (sp->occupant != -1)
            {
                score++;
            }
        }
        score -= int32_t(sq->orders.size());

        std::set<int32_t> units_to_kill;
        for (auto it : sq->orders)
        {
            if (auto so = strict_virtual_cast<df::squad_order_kill_listst>(it))
            {
                units_to_kill.insert(so->units.begin(), so->units.end());

                for (auto to_kill : so->units)
                {
                    if (u->id == to_kill)
                    {
                        score -= 100000;
                    }
                }
            }
        }

        for (auto oc : squad_order_changes)
        {
            if (oc.squad_id != sqid)
            {
                continue;
            }

            if (oc.type != squad_order_change::kill)
            {
                continue;
            }

            if (oc.unit_id == u->id)
            {
                if (oc.remove)
                {
                    score += 100000;
                }
                else
                {
                    score -= 100000;
                }
            }

            if (oc.remove)
            {
                units_to_kill.erase(oc.unit_id);
            }
            else
            {
                units_to_kill.insert(oc.unit_id);
            }
        }

        score -= 10000 * int32_t(units_to_kill.size());

        if (!squad || best < score)
        {
            squad = sq;
            best = score;
        }
    }
    if (!squad)
    {
        return false;
    }

    return military_squad_attack_unit(out, squad, u, reason);
}

bool Population::military_all_squads_attack_unit(color_ostream & out, df::unit *u, const std::string & reason)
{
    bool any = false;
    for (auto sqid : ui->main.fortress_entity->squads)
    {
        if (military_squad_attack_unit(out, df::squad::find(sqid), u, reason))
            any = true;
    }
    return any;
}

bool Population::military_squad_attack_unit(color_ostream &, df::squad *squad, df::unit *u, const std::string & reason)
{
    if (Units::isOwnCiv(u))
    {
        return false;
    }

    auto order_change = std::find_if(squad_order_changes.begin(), squad_order_changes.end(), [squad, u](const squad_order_change & change) -> bool
    {
        return change.type == squad_order_change::kill &&
            change.squad_id == squad->id &&
            change.unit_id == u->id;
    });

    if (order_change != squad_order_changes.end())
    {
        if (order_change->remove)
        {
            squad_order_changes.erase(order_change);
        }
        else
        {
            return false;
        }
    }

    for (auto it : squad->orders)
    {
        if (auto so = strict_virtual_cast<df::squad_order_kill_listst>(it))
        {
            if (std::find(so->units.begin(), so->units.end(), u->id) != so->units.end())
            {
                return false;
            }
        }
    }

    squad_order_change change;
    change.type = squad_order_change::kill;
    change.squad_id = squad->id;
    change.unit_id = u->id;
    change.remove = false;
    change.reason = reason;

    squad_order_changes.push_back(change);

    if (!events.has_exclusive<MilitarySquadAttackExclusive>(true))
    {
        events.queue_exclusive(new MilitarySquadAttackExclusive(ai));
    }

    return true;
}

bool Population::military_cancel_attack_order(color_ostream & out, df::unit *u, const std::string & reason)
{
    bool any = false;
    for (auto sqid : ui->main.fortress_entity->squads)
    {
        if (military_cancel_attack_order(out, df::squad::find(sqid), u, reason))
            any = true;
    }
    return any;
}

bool Population::military_cancel_attack_order(color_ostream &, df::squad *squad, df::unit *u, const std::string & reason)
{
    auto order_change = std::find_if(squad_order_changes.begin(), squad_order_changes.end(), [squad, u](const squad_order_change & change) -> bool
    {
        return change.type == squad_order_change::kill &&
            change.squad_id == squad->id &&
            change.unit_id == u->id;
    });

    if (order_change != squad_order_changes.end())
    {
        if (order_change->remove)
        {
            return false;
        }
        else
        {
            squad_order_changes.erase(order_change);
        }
    }

    for (auto it : squad->orders)
    {
        if (auto so = strict_virtual_cast<df::squad_order_kill_listst>(it))
        {
            if (std::find(so->units.begin(), so->units.end(), u->id) == so->units.end())
            {
                return false;
            }
        }
    }

    squad_order_change change;
    change.type = squad_order_change::kill;
    change.squad_id = squad->id;
    change.unit_id = u->id;
    change.remove = true;
    change.reason = reason;

    squad_order_changes.push_back(change);

    if (!events.has_exclusive<MilitarySquadAttackExclusive>(true))
    {
        events.queue_exclusive(new MilitarySquadAttackExclusive(ai));
    }

    return true;
}
