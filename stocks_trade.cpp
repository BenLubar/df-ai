#include "ai.h"
#include "stocks.h"

#include "df/general_ref.h"
#include "df/item_foodst.h"

bool Stocks::willing_to_trade_item(color_ostream & out, df::item *item, bool checkContainer)
{
    for (auto ref : item->general_refs)
    {
        if (ref->getType() == general_ref_type::IS_ARTIFACT)
        {
            return false;
        }
        if (checkContainer && ref->getType() == general_ref_type::CONTAINED_IN_ITEM && willing_to_trade_item(out, ref->getItem()))
        {
            return false;
        }
    }

    switch (item->getType())
    {
    case item_type::FOOD:
        // sell meals, buy meal ingredients
        return true;
    case item_type::GOBLET:
    case item_type::AMULET:
    case item_type::BRACELET:
    case item_type::CROWN:
    case item_type::EARRING:
    case item_type::FIGURINE:
    case item_type::RING:
    case item_type::SCEPTER:
    case item_type::TOTEM:
        // crafts are high-value and easily replaceable
        return true;
    case item_type::GEM:
        // large gems are useless
        return true;
    default:
        break;
    }

    if (item->isFoodStorage() || item->getType() == item_type::BIN)
    {
        bool any_contents = false;

        for (auto ref : item->general_refs)
        {
            if (ref->getType() == general_ref_type::CONTAINS_ITEM)
            {
                any_contents = true;

                if (!willing_to_trade_item(out, ref->getItem(), false))
                {
                    return false;
                }
            }
        }

        return any_contents;
    }

    return false;
}

bool Stocks::want_trader_item(color_ostream &, df::item *item, const std::vector<df::item *> & already_want)
{
    if (item->hasSpecificImprovements(improvement_type::WRITING) || item->getType() == item_type::BOOK)
    {
        return true;
    }

    switch (item->getType())
    {
    case item_type::CAGE:
        for (auto ref : item->general_refs)
        {
            if (ref->getType() == general_ref_type::CONTAINS_UNIT)
            {
                return true;
            }
        }
        return false;
    case item_type::WOOD:
    case item_type::BOULDER:
    case item_type::BAR:
    case item_type::CLOTH:
    case item_type::SKIN_TANNED:
    case item_type::THREAD:
        // materials are always useful
        return true;
    case item_type::CHEESE:
    case item_type::EGG:
    case item_type::FISH:
    case item_type::FISH_RAW:
    case item_type::MEAT:
    case item_type::PLANT:
    case item_type::PLANT_GROWTH:
        // more food is always good
        return true;
    case item_type::INSTRUMENT:
        // let the dwarves have some fun
        return true;
    case item_type::ANVIL:
    {
        int32_t anvils_wanted = -int32_t(std::count_if(already_want.begin(), already_want.end(), [](df::item* i) -> bool { return i->getType() == item_type::ANVIL; }));
        anvils_wanted -= count_free[stock_item::anvil];
        ai.find_room(room_type::workshop, [&anvils_wanted](room *r) -> bool
            {
                if (r->workshop_type == workshop_type::MetalsmithsForge && !r->dfbuilding())
                {
                    anvils_wanted++;
                }
                return false;
            });

        return anvils_wanted >= 0;
    }
    default:
        return false;
    }
}

bool Stocks::want_trader_item_more(df::item *a, df::item *b)
{
    if (a->getType() == item_type::CAGE && b->getType() != item_type::CAGE)
    {
        return true;
    }
    else if (b->getType() == item_type::CAGE && a->getType() != item_type::CAGE)
    {
        return false;
    }

    if (a->getType() == item_type::WOOD && b->getType() != item_type::WOOD)
    {
        return true;
    }
    else if (b->getType() == item_type::WOOD && a->getType() != item_type::WOOD)
    {
        return false;
    }

    if (a->getType() == item_type::ANVIL && b->getType() != item_type::ANVIL)
    {
        return true;
    }
    else if (b->getType() == item_type::ANVIL && a->getType() != item_type::ANVIL)
    {
        return false;
    }

    if ((a->hasSpecificImprovements(improvement_type::WRITING) || a->getType() == item_type::BOOK) && !(b->hasSpecificImprovements(improvement_type::WRITING) || b->getType() == item_type::BOOK))
    {
        return true;
    }
    else if ((b->hasSpecificImprovements(improvement_type::WRITING) || b->getType() == item_type::BOOK) && !(a->hasSpecificImprovements(improvement_type::WRITING) || a->getType() == item_type::BOOK))
    {
        return false;
    }

    if (a->getType() == item_type::INSTRUMENT && b->getType() != item_type::INSTRUMENT)
    {
        return true;
    }
    else if (b->getType() == item_type::INSTRUMENT && a->getType() != item_type::INSTRUMENT)
    {
        return false;
    }

    return false;
}
