#include "ai.h"
#include "stocks.h"

#include "modules/Items.h"
#include "modules/Maps.h"

#include "df/building_actual.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_contained_in_itemst.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_unit_holderst.h"
#include "df/item_boulderst.h"
#include "df/reaction.h"
#include "df/reaction_reagent_itemst.h"
#include "df/reaction_product_itemst.h"
#include "df/unit.h"
#include "df/unit_inventory_item.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

// check if an item is free to use
bool Stocks::is_item_free(df::item *i, bool allow_nonempty)
{
    if (i->flags.bits.trader || // merchant's item
        i->flags.bits.in_job || // current job item
        i->flags.bits.construction ||
        i->flags.bits.encased ||
        i->flags.bits.removed || // deleted object
        i->flags.bits.forbid || // user forbidden (or dumped)
        i->flags.bits.dump ||
        i->flags.bits.in_chest) // in infirmary (XXX dwarf owned items ?)
    {
        return false;
    }

    if (i->flags.bits.container && !allow_nonempty)
    {
        // is empty
        for (auto ir = i->general_refs.begin(); ir != i->general_refs.end(); ir++)
        {
            if (virtual_cast<df::general_ref_contains_itemst>(*ir))
            {
                return false;
            }
        }
    }

    if (i->flags.bits.in_inventory)
    {
        // is not in a unit's inventory (ignore if it is simply hauled)
        for (auto ir : i->general_refs)
        {
            if (virtual_cast<df::general_ref_unit_holderst>(ir))
            {
                auto & inv = ir->getUnit()->inventory;
                for (auto ii : inv)
                {
                    if (ii->item == i && ii->mode != df::unit_inventory_item::Hauled)
                    {
                        return false;
                    }
                }
            }
            if (virtual_cast<df::general_ref_contained_in_itemst>(ir) && !is_item_free((ir)->getItem(), true))
            {
                return false;
            }
        }
    }

    if (i->flags.bits.in_building)
    {
        // is not part of a building construction materials
        for (auto ir = i->general_refs.begin(); ir != i->general_refs.end(); ir++)
        {
            if (virtual_cast<df::general_ref_building_holderst>(*ir))
            {
                auto & inv = virtual_cast<df::building_actual>((*ir)->getBuilding())->contained_items;
                for (auto bi = inv.begin(); bi != inv.end(); bi++)
                {
                    if ((*bi)->use_mode == 2 && (*bi)->item == i)
                    {
                        return false;
                    }
                }
            }
        }
    }

    df::coord pos = Items::getPosition(i);

    extern AI *dwarfAI; // XXX

    // If no dwarf can walk to it from the fort entrance, it's probably up in
    // a tree or down in the caverns.
    if (Maps::getTileWalkable(dwarfAI->fort_entrance_pos()) != Maps::getTileWalkable(pos))
    {
        return false;
    }

    df::tile_designation *td = Maps::getTileDesignation(pos);
    return td && !td->bits.hidden && td->bits.flow_size < 4;
}

bool Stocks::is_metal_ore(int32_t mi)
{
    return world->raws.inorganics[mi]->flags.is_set(inorganic_flags::METAL_ORE);
}

bool Stocks::is_metal_ore(df::item *i)
{
    if (virtual_cast<df::item_boulderst>(i) && i->getMaterial() == 0)
    {
        return is_metal_ore(i->getMaterialIndex());
    }
    return false;
}

std::string Stocks::is_raw_coke(int32_t mi)
{
    // mat_index => custom reaction name
    if (raw_coke.empty())
    {
        for (auto r : world->raws.reactions)
        {
            if (r->reagents.size() != 1)
                continue;

            int32_t mat;

            bool found = false;
            for (auto rr : r->reagents)
            {
                df::reaction_reagent_itemst *rri = virtual_cast<df::reaction_reagent_itemst>(rr);
                if (rri && rri->item_type == item_type::BOULDER && rri->mat_type == 0)
                {
                    mat = rri->mat_index;
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;

            found = false;
            for (auto rp : r->products)
            {
                df::reaction_product_itemst *rpi = virtual_cast<df::reaction_product_itemst>(rp);
                if (rpi && rpi->item_type == item_type::BAR && MaterialInfo(rpi).material->id == "COAL")
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;

            // XXX check input size vs output size ?
            raw_coke[mat] = r->code;
            raw_coke_inv[r->code] = mat;
        }
    }
    return raw_coke.count(mi) ? raw_coke.at(mi) : "";
}

std::string Stocks::is_raw_coke(df::item *i)
{
    if (virtual_cast<df::item_boulderst>(i) && i->getMaterial() == 0)
    {
        return is_raw_coke(i->getMaterialIndex());
    }
    return "";
}

bool Stocks::is_gypsum(int32_t mi)
{
    for (auto c = world->raws.inorganics[mi]->material.reaction_class.begin(); c != world->raws.inorganics[mi]->material.reaction_class.end(); c++)
    {
        if (**c == "GYPSUM") // XXX
        {
            return true;
        }
    }
    return false;
}

bool Stocks::is_gypsum(df::item *i)
{
    if (virtual_cast<df::item_boulderst>(i) && i->getMaterial() == 0)
    {
        return is_gypsum(i->getMaterialIndex());
    }
    return false;
}
