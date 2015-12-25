#pragma once

#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/histfig_entity_link_positionst.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/unit.h"

template<class F>
inline void unit_entity_positions(df::unit *unit, F func)
{
    auto hf = df::historical_figure::find(unit->hist_figure_id);
    if (!hf)
        return;

    for (auto link : hf->entity_links)
    {
        auto el = virtual_cast<df::histfig_entity_link_positionst>(link);
        if (!el)
            continue;
        auto ent = df::historical_entity::find(el->entity_id);
        if (!ent)
            continue;
        auto asn = binsearch_in_vector(ent->positions.assignments, el->assignment_id);
        if (!asn)
            continue;
        auto pos = binsearch_in_vector(ent->positions.own, asn->position_id);
        if (!pos)
            continue;
        func(pos);
    }
}

template<class T, class F>
inline T unit_entity_positions(df::unit *unit, T start, F func)
{
    unit_entity_positions(unit, [&start, func](df::entity_position *pos) -> void { start = func(start, pos); });
    return start;
}

// vim: et:sw=4:ts=4
