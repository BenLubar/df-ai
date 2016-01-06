#pragma once

#include <vector>

namespace df
{
    struct entity_position;
    struct unit;
}

std::vector<df::entity_position *> unit_entity_positions(df::unit *unit);

// vim: et:sw=4:ts=4
