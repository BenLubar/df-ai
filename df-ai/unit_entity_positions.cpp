#include "dfhack_shared.h"
#include "unit_entity_positions.h"

#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/histfig_entity_link_positionst.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/unit.h"

std::vector<df::entity_position *> unit_entity_positions(df::unit *unit)
{
    std::vector<df::entity_position *> list;
    for (auto el : df::historical_figure::find(unit->hist_figure_id)->entity_links)
    {
        df::histfig_entity_link_positionst *link = virtual_cast<df::histfig_entity_link_positionst>(el);
        if (!link)
            continue;
        df::historical_entity *ent = df::historical_entity::find(link->entity_id);
        df::entity_position_assignment *pa = binsearch_in_vector(ent->positions.assignments, link->assignment_id);
        list.push_back(binsearch_in_vector(ent->positions.own, pa->position_id));
    }
    return list;
}

// vim: et:sw=4:ts=4
