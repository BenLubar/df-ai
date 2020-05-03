#include "ai.h"
#include "population.h"
#include "plan.h"
#include "debug.h"

#include "modules/Gui.h"
#include "modules/Units.h"

#include "df/entity_position_assignment.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/layer_object_listst.h"
#include "df/squad.h"
#include "df/squad_schedule_order.h"
#include "df/ui.h"
#include "df/unit_skill.h"
#include "df/unit_soul.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_layer_noblelistst.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

bool Population::unit_hasmilitaryduty(df::unit *u)
{
    if (u->military.squad_id == -1)
    {
        return false;
    }
    df::squad *squad = df::squad::find(u->military.squad_id);
    std::vector<df::squad_schedule_order *> & curmonth = squad->schedule[squad->cur_alert_idx][*cur_year_tick / 28 / 1200]->orders;
    return !curmonth.empty() && (curmonth.size() != 1 || curmonth[0]->min_count != 0);
}

int32_t Population::unit_totalxp(const df::unit *u)
{
    int32_t t = 0;
    for (auto sk : u->status.current_soul->skills)
    {
        int32_t rat = sk->rating;
        t += 400 * rat + 100 * rat * (rat + 1) / 2 + sk->experience;
    }
    return t;
}

class AssignNoblesExclusive : public ExclusiveCallback
{
    AI & ai;
    df::entity_position_responsibility responsibility;

public:
    AssignNoblesExclusive(AI & ai, df::entity_position_responsibility responsibility) :
        ExclusiveCallback("assign noble position with responsibility " + enum_item_key(responsibility)),
        ai(ai),
        responsibility(responsibility)
    {
        dfplex_blacklist = true;
    }
    ~AssignNoblesExclusive() {}

    void Run(color_ostream & out)
    {
        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

        bool bookkeeper = responsibility == entity_position_responsibility::ACCOUNTING;

        std::vector<df::unit *> candidates;
        for (auto u : world->units.active)
        {
            if (!Units::isCitizen(u) || Units::isChild(u) || Units::isBaby(u))
            {
                continue;
            }

            if (bookkeeper && u->status.labors[unit_labor::MINE])
            {
                continue;
            }

            std::vector<Units::NoblePosition> positions;
            if (u->mood == mood_type::None && u->military.squad_id == -1 && !Units::getNoblePositions(&positions, u))
            {
                candidates.push_back(u);
            }
        }
        if (candidates.empty())
        {
            ai.debug(out, "Cannot assign noble position for " + enum_item_key(responsibility) + ": no candidates");
            return;
        }
        std::sort(candidates.begin(), candidates.end(), [this](df::unit *a, df::unit *b) -> bool
        {
            return ai.pop.unit_totalxp(a) > ai.pop.unit_totalxp(b);
        });

        Key(interface_key::D_NOBLES);

        ExpectedScreen<df::viewscreen_layer_noblelistst> view(this);

        ExpectScreen<df::viewscreen_layer_noblelistst>("layer_noblelist/List");

        bool found = false;

        for (size_t i = 0; i < view->assignments.size(); i++)
        {
            auto assignment = view->assignments.at(i);

            if (!assignment)
            {
                Key(interface_key::STANDARDSCROLL_DOWN);
                continue;
            }

            auto position = binsearch_in_vector(ui->main.fortress_entity->positions.own, assignment->position_id);
            if (!position || !position->responsibilities[responsibility])
            {
                Key(interface_key::STANDARDSCROLL_DOWN);
                continue;
            }

            found = true;

            auto hf = df::historical_figure::find(assignment->histfig);
            if (hf && hf->died_year != -1)
            {
                Key(interface_key::STANDARDSCROLL_DOWN);
                continue;
            }

            Key(interface_key::SELECT);

            ExpectScreen<df::viewscreen_layer_noblelistst>("layer_noblelist/Appoint");

            int found_candidate = -1;
            while (!candidates.empty() && found_candidate == -1)
            {
                auto candidate_unit = candidates.back();
                for (auto it = view->candidates.begin(); it != view->candidates.end(); it++)
                {
                    if ((*it)->unit == candidate_unit)
                    {
                        found_candidate = it - view->candidates.begin();
                        ai.debug(out, "Appointing " + AI::describe_unit(candidate_unit) + " as position " + position->name[0]);
                        break;
                    }
                }

                if (found_candidate == -1)
                {
                    ai.debug(out, "Discarding candidate " + AI::describe_unit(candidate_unit) + " for position " + position->name[0] + ": does not appear on candidate list");
                    candidates.pop_back();
                }
            }
            if (candidates.empty())
            {
                ai.debug(out, "Ran out of candidates for " + position->name[0] + "!");
                Key(interface_key::LEAVESCREEN);
                ExpectScreen<df::viewscreen_layer_noblelistst>("layer_noblelist/List");
                Key(interface_key::LEAVESCREEN);
                ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");
                return;
            }

            while (found_candidate > 0)
            {
                Key(interface_key::STANDARDSCROLL_DOWN);
                found_candidate--;
            }
            Key(interface_key::SELECT);
            ExpectScreen<df::viewscreen_layer_noblelistst>("layer_noblelist/List");

            if (bookkeeper && ui->bookkeeper_settings != 4)
            {
                Key(interface_key::NOBLELIST_SETTINGS);
                ExpectScreen<df::viewscreen_layer_noblelistst>("layer_noblelist/Settings");

                auto get_list = [&]() -> df::layer_object_listst *
                {
                    df::layer_object_listst *list = nullptr;
                    for (auto obj : view->layer_objects)
                    {
                        auto l = virtual_cast<df::layer_object_listst>(obj);
                        if (l && l->active)
                        {
                            list = l;
                            break;
                        }
                    }
                    DFAI_ASSERT(list, "could not find bookkeeper options list");
                    return list;
                };
                while (get_list() && get_list()->cursor != 4)
                {
                    Key(interface_key::STANDARDSCROLL_DOWN);
                }

                Key(interface_key::SELECT);
                Key(interface_key::LEAVESCREEN);
                ExpectScreen<df::viewscreen_layer_noblelistst>("layer_noblelist/List");
            }

            Key(interface_key::LEAVESCREEN);
            ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

            return;
        }

        if (found)
        {
            ai.debug(out, "Found position for " + enum_item_key(responsibility) + ", but it was already occupied.");
        }
        else
        {
            ai.debug(out, "Could not find position for " + enum_item_key(responsibility));
        }

        Key(interface_key::LEAVESCREEN);
        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");
    }
};

void Population::update_nobles(color_ostream & out)
{
    check_noble_apartments(out);

    if (!config.manage_nobles)
    {
        return;
    }

    for (auto & asn : ui->main.fortress_entity->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT])
    {
        auto hf = df::historical_figure::find(asn->histfig);
        auto doctor = df::unit::find(hf->unit_id);

        // Enable healthcare labors on chief medical officer.
        doctor->status.labors[unit_labor::DIAGNOSE] = true;
        doctor->status.labors[unit_labor::SURGERY] = true;
        doctor->status.labors[unit_labor::BONE_SETTING] = true;
        doctor->status.labors[unit_labor::SUTURING] = true;
        doctor->status.labors[unit_labor::DRESSING_WOUNDS] = true;
        doctor->status.labors[unit_labor::FEED_WATER_CIVILIANS] = true;
    }

#define WANT_POS(pos) \
    if (ui->main.fortress_entity->assignments_by_type[entity_position_responsibility::pos].empty()) \
    { \
        events.queue_exclusive(std::make_unique<AssignNoblesExclusive>(ai, entity_position_responsibility::pos)); \
    }

    WANT_POS(MANAGE_PRODUCTION);
    WANT_POS(ACCOUNTING);
    if (ai.find_room(room_type::infirmary, [](room *r) -> bool { return r->status != room_status::plan; }))
        WANT_POS(HEALTH_MANAGEMENT);
    WANT_POS(TRADE);
    WANT_POS(LAW_ENFORCEMENT);
    WANT_POS(EXECUTIONS);
}

void Population::check_noble_apartments(color_ostream & out)
{
    std::set<int32_t> noble_ids;

    for (auto asn : ui->main.fortress_entity->positions.assignments)
    {
        df::entity_position *pos = binsearch_in_vector(ui->main.fortress_entity->positions.own, asn->position_id);
        if (pos->required_office > 0 || pos->required_dining > 0 || pos->required_tomb > 0)
        {
            if (df::historical_figure *hf = df::historical_figure::find(asn->histfig))
            {
                noble_ids.insert(hf->unit_id);
            }
        }
    }

    ai.plan.attribute_noblerooms(out, noble_ids);
}
