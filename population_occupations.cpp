#include "ai.h"
#include "population.h"
#include "room.h"
#include "debug.h"

#include "modules/Gui.h"
#include "modules/Units.h"

#include "df/abstract_building_inn_tavernst.h"
#include "df/abstract_building_libraryst.h"
#include "df/abstract_building_templest.h"
#include "df/agreement.h"
#include "df/agreement_details.h"
#include "df/agreement_details_data_citizenship.h"
#include "df/agreement_details_data_location.h"
#include "df/agreement_details_data_parley.h"
#include "df/agreement_details_data_residency.h"
#include "df/agreement_party.h"
#include "df/building.h"
#include "df/d_init.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/occupation.h"
#include "df/ui.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_locationsst.h"
#include "df/viewscreen_petitionsst.h"
#include "df/world.h"
#include "df/world_site.h"

REQUIRE_GLOBAL(d_init);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

// with a population of 200:
const static int32_t wanted_tavern_keeper = 4;
const static int32_t wanted_tavern_keeper_min = 1;
const static int32_t wanted_tavern_performer = 8;
const static int32_t wanted_tavern_performer_min = 0;
const static int32_t wanted_library_scholar = 16;
const static int32_t wanted_library_scholar_min = 0;
const static int32_t wanted_library_scribe = 2;
const static int32_t wanted_library_scribe_min = 0;
const static int32_t wanted_temple_performer = 4;
const static int32_t wanted_temple_performer_min = 0;

class AssignOccupationExclusive : public ExclusiveCallback
{
    AI & ai;
    int32_t location_id;
    df::occupation_type occupation;

    static df::abstract_building *get_location(int32_t location_id)
    {
        auto site = ui->main.fortress_site;
        if (!site)
            return nullptr;

        return binsearch_in_vector(site->buildings, location_id);
    }
    static std::string get_location_name(int32_t location_id)
    {
        auto location = get_location(location_id);
        if (!location)
            return "(unknown location)";

        auto name = location->getName();
        if (!name)
            return "(unnamed " + enum_item_key(location->getType()) + ")";

        return AI::describe_name(*name, true);
    }

public:
    AssignOccupationExclusive(AI & ai, int32_t location_id, df::occupation_type occupation) :
        ExclusiveCallback("assign new " + enum_item_key(occupation) + " at " + get_location_name(location_id)),
        ai(ai),
        location_id(location_id),
        occupation(occupation)
    {
    }

    void Run(color_ostream & out)
    {
        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

        Key(interface_key::D_LOCATIONS);

        ExpectedScreen<df::viewscreen_locationsst> view(this);

        ExpectScreen<df::viewscreen_locationsst>("locations/Locations");

        bool found = false;
        for (auto loc : view->locations)
        {
            if (loc && loc->id == location_id)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            ai.debug(out, "[ERROR] could not find location " + get_location_name(location_id) + " on the list");

            Key(interface_key::LEAVESCREEN);

            ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

            return;
        }

        while (!view->locations.at(view->location_idx) || view->locations.at(view->location_idx)->id != location_id)
        {
            Key(interface_key::STANDARDSCROLL_DOWN);
        }

        Key(interface_key::STANDARDSCROLL_RIGHT);

        ExpectScreen<df::viewscreen_locationsst>("locations/Occupations");

        found = false;
        for (auto occ : view->occupations)
        {
            if (occ->unit_id == -1 && occ->type == occupation)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            ai.debug(out, "[ERROR] could not find occupation " + enum_item_key(occupation) + " on the list");

            Key(interface_key::LEAVESCREEN);

            ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

            return;
        }

        while (view->occupations.at(view->occupation_idx)->unit_id != -1 || view->occupations.at(view->occupation_idx)->type != occupation)
        {
            Key(interface_key::STANDARDSCROLL_DOWN);
        }

        Key(interface_key::SELECT);

        ExpectScreen<df::viewscreen_locationsst>("locations/AssignOccupation");

        df::unit *chosen = nullptr;
        int32_t best = std::numeric_limits<int32_t>::max();
        for (auto u : view->units)
        {
            if (!u || u->military.squad_id != -1)
            {
                continue;
            }

            std::vector<Units::NoblePosition> positions;
            Units::getNoblePositions(&positions, u);
            if (!positions.empty())
            {
                continue;
            }

            bool has_occupation = false;
            for (auto o : world->occupations.all)
            {
                if (o->unit_id == u->id)
                {
                    has_occupation = true;
                    break;
                }
            }

            if (has_occupation)
            {
                continue;
            }

            int32_t score = ai.pop.unit_totalxp(u);
            if (!chosen || score < best)
            {
                chosen = u;
                best = score;
            }
        }

        if (!chosen)
        {
            ai.debug(out, "[ERROR] could not find unit for occupation " + enum_item_key(occupation) + " at " + get_location_name(location_id));

            Key(interface_key::LEAVESCREEN);

            ExpectScreen<df::viewscreen_locationsst>("locations/Occupations");

            Key(interface_key::LEAVESCREEN);

            ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

            return;
        }

        ai.debug(out, "[Population] assigning occupation " + enum_item_key(occupation) + " at " + get_location_name(location_id) + " to " + AI::describe_unit(chosen));

        while (view->units.at(view->unit_idx) != chosen)
        {
            Key(interface_key::STANDARDSCROLL_DOWN);
        }

        Key(interface_key::SELECT);

        ExpectScreen<df::viewscreen_locationsst>("locations/Occupations");

        Key(interface_key::LEAVESCREEN);

        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");
    }
};

class CheckPetitionsExclusive : public ExclusiveCallback
{
    AI & ai;

public:
    CheckPetitionsExclusive(AI & ai) :
        ExclusiveCallback("check petitions"),
        ai(ai)
    {
    }

    void Run(color_ostream & out)
    {
        if (ui->petitions.empty())
            return;

        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

        Key(interface_key::D_PETITIONS);

        ExpectedScreen<df::viewscreen_petitionsst> view(this);

        ExpectScreen<df::viewscreen_petitionsst>("petitions");

        if (!view->can_manage)
        {
            ai.debug(out, "[Population] nobody is available to manage petitions.");

            Key(interface_key::LEAVESCREEN);

            ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

            return;
        }

        while (!view->list.empty())
        {
            auto petition = view->list.at(view->cursor);
            auto details = petition->details.at(0);

            auto describe_party = [petition](int32_t party_id) -> std::string
            {
                if (petition->parties.size() <= size_t(party_id))
                    return "(unknown)";

                auto party = petition->parties.at(party_id);

                if (!party->entity_ids.empty())
                {
                    auto ent = df::historical_entity::find(party->entity_ids.at(0));
                    auto name = ent ? &ent->name : nullptr;
                    return name ? AI::describe_name(*name, true) : "(unknown)";
                }

                if (!party->histfig_ids.empty())
                {
                    auto fig = df::historical_figure::find(party->histfig_ids.at(0));
                    auto name = fig ? &fig->name : nullptr;
                    return name ? AI::describe_name(*name, false) : "(unknown)";
                }

                return "(unknown)";
            };

            switch (details->type)
            {
                case agreement_details_type::Residency:
                    ai.debug(out, "[Population] accepting residency petition from " + describe_party(details->data.Residency->applicant));
                    Key(interface_key::OPTION1);
                    break;
                case agreement_details_type::Citizenship:
                    ai.debug(out, "[Population] accepting citizenship petition from " + describe_party(details->data.Citizenship->applicant));
                    Key(interface_key::OPTION1);
                    break;
                case agreement_details_type::Parley:
                    // TODO: consider implementing this
                    ai.debug(out, "[Population] rejecting petition for a parley from " + describe_party(details->data.Parley->party_id));
                    Key(interface_key::OPTION2);
                    break;
                case agreement_details_type::Location:
                {
                    auto loc = details->data.Location;
                    int32_t data1 = -1;
                    int32_t data2 = -1;
                    int32_t required_value;
                    location_type::type type;
                    switch (loc->type)
                    {
                        case abstract_building_type::TEMPLE:
                            required_value = d_init->temple_value_levels[loc->tier - 1];
                            type = location_type::temple;
                            data1 = int32_t(loc->deity_type);
                            data2 = loc->deity_data.Deity;
                            break;
                        case abstract_building_type::GUILDHALL:
                            required_value = d_init->guildhall_value_levels[loc->tier - 1];
                            type = location_type::guildhall;
                            data1 = loc->profession;
                            break;
                        default:
                            ai.debug(out, "[Population] rejecting petition to construct a " + toLower(enum_item_key(loc->type)) + " from " + describe_party(loc->applicant) + ": don't know how to construct this location for a petition");
                            Key(interface_key::OPTION2);
                            continue;
                    }

                    auto found_room = ai.find_room(room_type::location, [type](room *r) -> bool { return r->status == room_status::plan && !r->queue_dig && r->location_type == type; });
                    if (!found_room)
                    {
                        ai.debug(out, "[Population] rejecting petition to construct a " + toLower(enum_item_key(loc->type)) + " from " + describe_party(loc->applicant) + ": no remaining space for buildings of this type");
                        Key(interface_key::OPTION2);
                        break;
                    }

                    found_room->data1 = data1;
                    found_room->data2 = data2;
                    found_room->required_value = required_value;
                    ai.plan.wantdig(out, found_room, -2);

                    ai.debug(out, "[Population] accepting petition to construct a " + toLower(enum_item_key(loc->type)) + " from " + describe_party(loc->applicant));
                    Key(interface_key::OPTION1);
                    break;
                }
                default:
                    DFAI_ASSERT(false, "unexpected petition type: " + enum_item_key(details->type));

                    Key(interface_key::LEAVESCREEN);

                    ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");
                    return;
            }
        }

        Key(interface_key::LEAVESCREEN);

        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");
    }
};

void Population::update_locations(color_ostream &)
{
    if (!ui->petitions.empty())
    {
        events.queue_exclusive(std::make_unique<CheckPetitionsExclusive>(ai));
    }

#define INIT_NEED(name) int32_t need_##name = std::max(wanted_##name * int32_t(citizen.size()) / 200, wanted_##name##_min)
    INIT_NEED(tavern_keeper);
    INIT_NEED(tavern_performer);
    INIT_NEED(library_scholar);
    INIT_NEED(library_scribe);
    INIT_NEED(temple_performer);
#undef INIT_NEED

    if (room *tavern = ai.find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::tavern && r->dfbuilding(); }))
    {
        df::building *bld = tavern->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_inn_tavernst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto occ : loc->occupations)
            {
                if (occ->unit_id != -1)
                {
                    if (occ->type == occupation_type::TAVERN_KEEPER)
                    {
                        need_tavern_keeper--;
                    }
                    else if (occ->type == occupation_type::PERFORMER)
                    {
                        need_tavern_performer--;
                    }
                }
            }
            if (need_tavern_keeper > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::TAVERN_KEEPER));
            }
            if (need_tavern_performer > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::PERFORMER));
            }
        }
    }

    if (room *library = ai.find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::library && r->dfbuilding(); }))
    {
        df::building *bld = library->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_libraryst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto occ : loc->occupations)
            {
                if (occ->unit_id != -1)
                {
                    if (occ->type == occupation_type::SCHOLAR)
                    {
                        need_library_scholar--;
                    }
                    else if (occ->type == occupation_type::SCRIBE)
                    {
                        need_library_scribe--;
                    }
                }
            }
            if (need_library_scholar > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::SCHOLAR));
            }
            if (need_library_scribe > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::SCRIBE));
            }
        }
    }

    if (room *temple = ai.find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::temple && r->dfbuilding(); }))
    {
        df::building *bld = temple->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_templest>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto occ : loc->occupations)
            {
                if (occ->unit_id != -1)
                {
                    if (occ->type == occupation_type::PERFORMER)
                    {
                        need_temple_performer--;
                    }
                }
            }
            if (need_temple_performer > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::PERFORMER));
            }
        }
    }
}
