#include "ai.h"
#include "stocks.h"

#include "df/general_ref.h"
#include "df/item_foodst.h"

bool Stocks::willing_to_trade_item(color_ostream & out, df::item *item)
{
    if (virtual_cast<df::item_foodst>(item))
    {
        return true;
    }

    if (item->isFoodStorage())
    {
        bool any_contents = false;

        for (auto ref : item->general_refs)
        {
            if (ref->getType() == general_ref_type::CONTAINS_ITEM)
            {
                any_contents = true;

                if (!willing_to_trade_item(out, ref->getItem()))
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

    if (item->getType() == item_type::WOOD || item->getType() == item_type::BOULDER || item->getType() == item_type::BAR)
    {
        return true;
    }

    if (item->getType() == item_type::CLOTH || item->getType() == item_type::SKIN_TANNED || item->getType() == item_type::THREAD)
    {
        return true;
    }

    if (item->getType() == item_type::CHEESE || item->getType() == item_type::EGG || item->getType() == item_type::FISH || item->getType() == item_type::FISH_RAW || item->getType() == item_type::MEAT || item->getType() == item_type::PLANT || item->getType() == item_type::PLANT_GROWTH)
    {
        return true;
    }

    if (item->getType() == item_type::INSTRUMENT)
    {
        return true;
    }

    if (item->getType() == item_type::ANVIL)
    {
        int32_t anvils_wanted = -int32_t(std::count_if(already_want.begin(), already_want.end(), [](df::item *i) -> bool { return i->getType() == item_type::ANVIL; }));
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

    return false;
}

bool Stocks::want_trader_item_more(df::item *a, df::item *b)
{
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
