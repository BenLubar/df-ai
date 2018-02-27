#include "ai.h"
#include "stocks.h"

#include "modules/Materials.h"

#include "df/entity_raw.h"
#include "df/historical_entity.h"
#include "df/inorganic_raw.h"
#include "df/item_armorst.h"
#include "df/item_glovesst.h"
#include "df/item_helmst.h"
#include "df/item_pantsst.h"
#include "df/item_shieldst.h"
#include "df/item_shoesst.h"
#include "df/item_weaponst.h"
#include "df/itemdef_armorst.h"
#include "df/itemdef_glovesst.h"
#include "df/itemdef_helmst.h"
#include "df/itemdef_pantsst.h"
#include "df/itemdef_shieldst.h"
#include "df/itemdef_shoesst.h"
#include "df/itemdef_weaponst.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/strain_type.h"
#include "df/ui.h"
#include "df/world.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

// forge weapons
void Stocks::queue_need_weapon(color_ostream & out, stock_item::item stock_item, int32_t needed, std::ostream & reason, df::job_skill skill, bool training, bool ranged, bool digger)
{
    if (skill == job_skill::NONE && !training && (count_free.at(stock_item::pick) == 0 || count_free.at(stock_item::axe) == 0))
    {
        reason << "need more ";
        if (count_free.at(stock_item::pick) == 0)
        {
            reason << "picks";
        }
        if (count_free.at(stock_item::axe) == 0)
        {
            if (count_free.at(stock_item::pick) == 0)
            {
                reason << " and ";
            }
            reason << "axes";
        }
        return;
    }

    auto search = [this, &out, stock_item, needed, &reason, skill, training, ranged](const std::vector<int16_t> & idefs, df::material_flags pref)
    {
        for (auto id : idefs)
        {
            df::itemdef_weaponst *idef = df::itemdef_weaponst::find(id);
            if (skill != job_skill::NONE && (ranged ? idef->skill_ranged : idef->skill_melee) != skill)
                continue;
            if (ranged == (idef->skill_ranged == job_skill::NONE))
                continue;
            if (idef->flags.is_set(weapon_flags::TRAINING) != training)
                continue;

            int32_t cnt = needed;

            for (auto item : world->items.other[items_other_id::WEAPON])
            {
                df::item_weaponst *i = virtual_cast<df::item_weaponst>(item);
                if (i->subtype->subtype == idef->subtype && is_item_free(i))
                {
                    cnt--;
                }
            }
            if (cnt <= 0)
            {
                continue;
            }

            for (auto mo : world->manager_orders)
            {
                if (mo->job_type == job_type::MakeWeapon && mo->item_subtype == idef->subtype)
                {
                    cnt -= mo->amount_left;
                }
            }
            events.each_exclusive<ManagerOrderExclusive>([&cnt, idef](const ManagerOrderExclusive *excl) -> bool
            {
                if (excl->tmpl.job_type == job_type::MakeWeapon && excl->tmpl.item_subtype == idef->subtype)
                {
                    cnt -= excl->amount;
                }
                return false;
            });

            if (cnt <= 0)
            {
                reason << "already have manager order for " << idef->name << "\n";
                continue;
            }

            if (training)
            {
                df::manager_order_template tmpl;
                tmpl.job_type = job_type::MakeWeapon;
                tmpl.item_type = item_type::NONE;
                tmpl.item_subtype = idef->subtype;
                tmpl.mat_index = -1;
                tmpl.material_category.bits.wood = true;
                add_manager_order(out, tmpl, cnt, reason);
                continue;
            }

            int32_t need_bars = idef->material_size / 3; // need this many bars to forge one idef item
            if (need_bars < 1)
                need_bars = 1;

            queue_need_forge(out, pref, need_bars, stock_item, job_type::MakeWeapon, [this, &out, &reason, pref, need_bars](const std::map<int32_t, int32_t> & potential_bars, const std::map<int32_t, int32_t> & actual_bars, int32_t & chosen_type) -> bool
            {
                std::vector<int32_t> best;
                best.insert(best.end(), metal_pref.at(pref).begin(), metal_pref.at(pref).end());
                std::sort(best.begin(), best.end(), [need_bars](int32_t a, int32_t b) -> bool
                {
                    // should roughly order metals by effectiveness
                    return world->raws.inorganics[a]->material.strength.yield[strain_type::IMPACT] > world->raws.inorganics[b]->material.strength.yield[strain_type::IMPACT];
                });

                for (auto mi : best)
                {
                    if (potential_bars.count(mi))
                    {
                        chosen_type = mi;
                        return true;
                    }

                    std::ostringstream may_forge_reason;
                    if (may_forge_bars(out, mi, may_forge_reason, need_bars) > 0)
                    {
                        reason << may_forge_reason.str();
                        return false;
                    }
                }
                return false;
            }, reason, item_type::NONE, idef->subtype);
        }
    };

    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    if (digger)
    {
        search(ue.digger_id, material_flags::ITEMS_DIGGER);
    }
    else
    {
        search(ue.weapon_id, material_flags::ITEMS_WEAPON);
    }
}

template<typename D>
static bool is_armordef_metal(D *d) { return d->props.flags.is_set(armor_general_flags::METAL); }

template<typename D, typename I>
static void queue_need_armor_helper(AI *ai, color_ostream & out, stock_item::item what, df::items_other_id oidx, const std::vector<int16_t> & idefs, df::job_type job, std::ostream & reason, int32_t div = 1, std::function<bool(D *)> pred = is_armordef_metal<D>)
{
    for (auto id : idefs)
    {
        auto idef = D::find(id);

        if (!pred(idef))
        {
            continue;
        }

        int32_t cnt = ai->stocks->num_needed(what);
        int32_t have = 0;
        for (auto item : world->items.other[oidx])
        {
            auto i = virtual_cast<I>(item);
            if (i && i->subtype->subtype == idef->subtype && i->mat_type == 0 && ai->stocks->is_item_free(i))
            {
                have++;
            }
        }
        cnt -= have / div;

        for (auto mo : world->manager_orders)
        {
            if (mo->job_type == job && mo->item_subtype == idef->subtype)
            {
                cnt -= mo->amount_total;
            }
        }
        events.each_exclusive<ManagerOrderExclusive>([&cnt, job, idef](const ManagerOrderExclusive *excl) -> bool
        {
            if (excl->tmpl.job_type == job && excl->tmpl.item_subtype == idef->subtype)
            {
                cnt -= excl->amount;
            }
            return false;
        });

        if (cnt <= 0)
        {
            reason << "already have manager order for " << idef->name << "\n";
            continue;
        }

        int32_t need_bars = idef->material_size / 3; // need this many bars to forge one idef item
        if (need_bars < 1)
            need_bars = 1;

        ai->stocks->queue_need_forge(out, material_flags::ITEMS_ARMOR, need_bars, what, job, [ai, &out, &reason, need_bars](const std::map<int32_t, int32_t> & potential_bars, const std::map<int32_t, int32_t> & actual_bars, int32_t & chosen_type) -> bool
        {
            std::vector<int32_t> best;
            const auto & pref = ai->stocks->metal_pref.at(material_flags::ITEMS_ARMOR);
            best.insert(best.end(), pref.begin(), pref.end());
            std::sort(best.begin(), best.end(), [](int32_t a, int32_t b) -> bool
            {
                // should roughly order metals by effectiveness
                return world->raws.inorganics[a]->material.strength.yield[strain_type::IMPACT] > world->raws.inorganics[b]->material.strength.yield[strain_type::IMPACT];
            });

            for (auto mi : best)
            {
                if (potential_bars.count(mi))
                {
                    chosen_type = mi;
                    return true;
                }

                std::ostringstream may_forge_reason;
                if (ai->stocks->may_forge_bars(out, mi, may_forge_reason, need_bars) > 0)
                {
                    reason << may_forge_reason.str();
                    return false;
                }
            }
            return false;
        }, reason, item_type::NONE, idef->subtype);
    }
}

// forge armor pieces
void Stocks::queue_need_armor(color_ostream & out, stock_item::item what, df::items_other_id oidx, std::ostream & reason)
{
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;

    switch (oidx)
    {
    case items_other_id::ARMOR:
        queue_need_armor_helper<df::itemdef_armorst, df::item_armorst>(ai, out, what, oidx, ue.armor_id, job_type::MakeArmor, reason);
        return;
    case items_other_id::SHIELD:
        queue_need_armor_helper<df::itemdef_shieldst, df::item_shieldst>(ai, out, what, oidx, ue.shield_id, job_type::MakeShield, reason, 1, [](df::itemdef_shieldst *) -> bool { return true; });
        return;
    case items_other_id::HELM:
        queue_need_armor_helper<df::itemdef_helmst, df::item_helmst>(ai, out, what, oidx, ue.helm_id, job_type::MakeHelm, reason);
        return;
    case items_other_id::PANTS:
        queue_need_armor_helper<df::itemdef_pantsst, df::item_pantsst>(ai, out, what, oidx, ue.pants_id, job_type::MakePants, reason);
        return;
    case items_other_id::GLOVES:
        queue_need_armor_helper<df::itemdef_glovesst, df::item_glovesst>(ai, out, what, oidx, ue.gloves_id, job_type::MakeGloves, reason, 2);
        return;
    case items_other_id::SHOES:
        queue_need_armor_helper<df::itemdef_shoesst, df::item_shoesst>(ai, out, what, oidx, ue.shoes_id, job_type::MakeGloves, reason, 2);
        return;
    default:
        return;
    }
}

template<typename D, typename I>
static void queue_need_clothes_helper(AI *ai, color_ostream & out, df::items_other_id oidx, const std::vector<int16_t> & idefs, int32_t & available_cloth, df::job_type job, int32_t needed, std::ostream & reason, int32_t div = 1)
{
    int32_t thread = 0, yarn = 0, silk = 0;

    for (auto & item : world->items.other[items_other_id::CLOTH])
    {
        MaterialInfo mat(item);
        if (mat.material)
        {
            if (mat.material->flags.is_set(material_flags::THREAD_PLANT))
            {
                thread++;
            }
            else if (mat.material->flags.is_set(material_flags::SILK))
            {
                silk++;
            }
            else if (mat.material->flags.is_set(material_flags::YARN))
            {
                yarn++;
            }
        }
    }

    bool first = true;
    for (auto id : idefs)
    {
        auto idef = D::find(id);
        if (!idef->props.flags.is_set(armor_general_flags::SOFT)) // XXX
        {
            continue;
        }

        int32_t cnt = needed;
        int32_t have = 0;
        for (auto item : world->items.other[oidx])
        {
            auto i = virtual_cast<I>(item);
            if (i->subtype->subtype == idef->subtype &&
                i->mat_type != 0 &&
                i->wear == 0 &&
                ai->stocks->is_item_free(i))
            {
                have++;
            }
        }
        cnt -= have / div;

        bool first_def = true;
        for (auto mo : world->manager_orders)
        {
            if (mo->job_type == job && mo->item_subtype == idef->subtype)
            {
                cnt -= mo->amount_left;

                if (first_def)
                {
                    first_def = false;

                    if (first)
                    {
                        first = false;
                    }

                    reason << "already have manager order for " << idef->name << "\n";
                }
            }
        }
        events.each_exclusive<ManagerOrderExclusive>([&](const ManagerOrderExclusive *excl) -> bool
        {
            if (excl->tmpl.job_type == job && excl->tmpl.item_subtype == idef->subtype)
            {
                cnt -= excl->amount;

                if (first_def)
                {
                    first_def = false;

                    if (first)
                    {
                        first = false;
                    }

                    reason << "already have manager order for " << idef->name << "\n";
                }
            }

            return false;
        });
        // TODO subtract available_cloth too
        if (cnt > available_cloth)
            cnt = available_cloth;
        if (cnt <= 0)
            continue;

        df::manager_order_template tmpl;
        tmpl.job_type = job;
        tmpl.item_type = item_type::NONE;
        tmpl.item_subtype = idef->subtype;
        tmpl.mat_type = -1;
        tmpl.mat_index = -1;

        if (thread >= yarn && thread >= silk)
        {
            tmpl.material_category.bits.cloth = 1;
            thread -= cnt;
        }
        else if (yarn >= thread && yarn >= silk)
        {
            tmpl.material_category.bits.yarn = 1;
            yarn -= cnt;
        }
        else if (silk >= thread && silk >= yarn)
        {
            tmpl.material_category.bits.silk = 1;
            silk -= cnt;
        }

        if (first)
        {
            first = false;
        }
        ai->stocks->add_manager_order(out, tmpl, cnt, reason);
        reason << "\n";

        available_cloth -= cnt;
    }

    if (first)
    {
        reason << "not enough cloth";
    }
}

void Stocks::queue_need_clothes(color_ostream & out, df::items_other_id oidx, std::ostream & reason)
{
    // try to avoid cancel spam
    int32_t available_cloth = count_free.at(stock_item::cloth) - 20;

    auto & ue = ui->main.fortress_entity->entity_raw->equipment;

    switch (oidx)
    {
    case items_other_id::ARMOR:
        queue_need_clothes_helper<df::itemdef_armorst, df::item_armorst>(ai, out, oidx, ue.armor_id, available_cloth, job_type::MakeArmor, num_needed(stock_item::clothes_torso), reason);
        return;
    case items_other_id::HELM:
        queue_need_clothes_helper<df::itemdef_helmst, df::item_helmst>(ai, out, oidx, ue.helm_id, available_cloth, job_type::MakeHelm, num_needed(stock_item::clothes_head), reason);
        return;
    case items_other_id::PANTS:
        queue_need_clothes_helper<df::itemdef_pantsst, df::item_pantsst>(ai, out, oidx, ue.pants_id, available_cloth, job_type::MakePants, num_needed(stock_item::clothes_legs), reason);
        return;
    case items_other_id::GLOVES:
        queue_need_clothes_helper<df::itemdef_glovesst, df::item_glovesst>(ai, out, oidx, ue.gloves_id, available_cloth, job_type::MakeGloves, num_needed(stock_item::clothes_hands), reason, 2);
        return;
    case items_other_id::SHOES:
        queue_need_clothes_helper<df::itemdef_shoesst, df::item_shoesst>(ai, out, oidx, ue.shoes_id, available_cloth, job_type::MakeShoes, num_needed(stock_item::clothes_feet), reason, 2);
        return;
    default:
        return;
    }
}
