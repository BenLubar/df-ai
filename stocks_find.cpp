#include "stocks.h"

#include "Error.h"

#include "modules/Materials.h"

#include "df/building_trapst.h"
#include "df/entity_raw.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/historical_entity.h"
#include "df/item_ammost.h"
#include "df/item_animaltrapst.h"
#include "df/item_armorst.h"
#include "df/item_barrelst.h"
#include "df/item_binst.h"
#include "df/item_boxst.h"
#include "df/item_bucketst.h"
#include "df/item_cagest.h"
#include "df/item_clothst.h"
#include "df/item_corpsepiecest.h"
#include "df/item_flaskst.h"
#include "df/item_foodst.h"
#include "df/item_globst.h"
#include "df/item_glovesst.h"
#include "df/item_helmst.h"
#include "df/item_pantsst.h"
#include "df/item_shieldst.h"
#include "df/item_shoesst.h"
#include "df/item_threadst.h"
#include "df/item_toolst.h"
#include "df/item_trapcompst.h"
#include "df/item_weaponst.h"
#include "df/itemdef_armorst.h"
#include "df/itemdef_glovesst.h"
#include "df/itemdef_helmst.h"
#include "df/itemdef_pantsst.h"
#include "df/itemdef_shieldst.h"
#include "df/itemdef_shoesst.h"
#include "df/itemdef_toolst.h"
#include "df/itemdef_trapcompst.h"
#include "df/itemdef_weaponst.h"
#include "df/itemimprovement.h"
#include "df/ui.h"
#include "df/vehicle.h"
#include "df/world.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

df::item *Stocks::find_free_item(stock_item::item k)
{
    auto helper = find_item_helper(k);

    for (auto item : world->items.other[helper.first])
    {
        if (helper.second(item) && is_item_free(item))
        {
            return item;
        }
    }

    return nullptr;
}

std::pair<df::items_other_id, std::function<bool(df::item *)>> Stocks::find_item_helper(stock_item::item k)
{
    static std::function<bool(df::item *)> yes_i_mean_all = [](df::item *) -> bool { return true; };

    switch (k)
    {
    case stock_item::anvil:
    {
        return std::make_pair(items_other_id::ANVIL, yes_i_mean_all);
    }
    case stock_item::armor_feet:
    {
        return find_item_helper_armor(items_other_id::SHOES);
    }
    case stock_item::armor_hands:
    {
        return find_item_helper_armor(items_other_id::GLOVES);
    }
    case stock_item::armor_head:
    {
        return find_item_helper_armor(items_other_id::HELM);
    }
    case stock_item::armor_legs:
    {
        return find_item_helper_armor(items_other_id::PANTS);
    }
    case stock_item::armor_shield:
    {
        return find_item_helper_armor(items_other_id::SHIELD);
    }
    case stock_item::armor_stand:
    {
        return std::make_pair(items_other_id::ARMORSTAND, yes_i_mean_all);
    }
    case stock_item::armor_torso:
    {
        return find_item_helper_armor(items_other_id::ARMOR);
    }
    case stock_item::ash:
    {
        return std::make_pair(items_other_id::BAR, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "ASH";
        });
    }
    case stock_item::axe:
    {
        return find_item_helper_weapon(job_skill::AXE);
    }
    case stock_item::backpack:
    {
        return std::make_pair(items_other_id::BACKPACK, yes_i_mean_all);
    }
    case stock_item::bag:
    {
        return std::make_pair(items_other_id::BOX, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.isAnyCloth() || mat.material->flags.is_set(material_flags::LEATHER);
        });
    }
    case stock_item::bag_plant:
    {
        return std::make_pair(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return bag_plants.count(i->getMaterialIndex()) && bag_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::barrel:
    {
        return std::make_pair(items_other_id::BARREL, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_barrelst>(i)->stockpile.id == -1;
        });
    }
    case stock_item::bed:
    {
        return std::make_pair(items_other_id::BED, yes_i_mean_all);
    }
    case stock_item::bin:
    {
        return std::make_pair(items_other_id::BIN, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_binst>(i)->stockpile.id == -1;
        });
    }
    case stock_item::block:
    {
        return std::make_pair(items_other_id::BLOCKS, yes_i_mean_all);
    }
    case stock_item::bone:
    {
        return std::make_pair(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            return i->corpse_flags.bits.bone && !i->corpse_flags.bits.unbutchered;
        });
    }
    case stock_item::bone_bolts:
    {
        return std::make_pair(items_other_id::AMMO, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_ammost>(i)->skill_used == job_skill::BONECARVE;
        });
    }
    case stock_item::book_binding:
    {
        return find_item_helper_tool(stock_item::book_binding);
    }
    case stock_item::bookcase:
    {
        return find_item_helper_tool(stock_item::bookcase);
    }
    case stock_item::bucket:
    {
        return std::make_pair(items_other_id::BUCKET, yes_i_mean_all);
    }
    case stock_item::cabinet:
    {
        return std::make_pair(items_other_id::CABINET, yes_i_mean_all);
    }
    case stock_item::cage:
    {
        return std::make_pair(items_other_id::CAGE, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            if (!mat.material || mat.material->flags.is_set(material_flags::IS_METAL))
            {
                return false;
            }

            for (auto ref = i->general_refs.begin(); ref != i->general_refs.end(); ref++)
            {
                if (virtual_cast<df::general_ref_contains_unitst>(*ref))
                    return false;
                if (virtual_cast<df::general_ref_contains_itemst>(*ref))
                    return false;
                df::general_ref_building_holderst *bh = virtual_cast<df::general_ref_building_holderst>(*ref);
                if (bh && virtual_cast<df::building_trapst>(bh->getBuilding()))
                    return false;
            }
            return true;
        });
    }
    case stock_item::cage_metal:
    {
        return std::make_pair(items_other_id::CAGE, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            if (!mat.material || !mat.material->flags.is_set(material_flags::IS_METAL))
            {
                return false;
            }

            for (auto ref = i->general_refs.begin(); ref != i->general_refs.end(); ref++)
            {
                if (virtual_cast<df::general_ref_contains_unitst>(*ref))
                    return false;
                if (virtual_cast<df::general_ref_contains_itemst>(*ref))
                    return false;
                df::general_ref_building_holderst *bh = virtual_cast<df::general_ref_building_holderst>(*ref);
                if (bh && virtual_cast<df::building_trapst>(bh->getBuilding()))
                    return false;
            }
            return true;
        });
    }
    case stock_item::chair:
    {
        return std::make_pair(items_other_id::CHAIR, yes_i_mean_all);
    }
    case stock_item::chest:
    {
        return std::make_pair(items_other_id::BOX, [](df::item *i) -> bool
        {
            return i->getMaterial() == 0;
        });
    }
    case stock_item::clay:
    {
        return std::make_pair(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return clay_stones.count(i->getMaterialIndex());
        });
    }
    case stock_item::cloth:
    {
        return std::make_pair(items_other_id::CLOTH, yes_i_mean_all);
    }
    case stock_item::cloth_nodye:
    {
        return std::make_pair(items_other_id::CLOTH, [this](df::item *i) -> bool
        {
            df::item_clothst *c = virtual_cast<df::item_clothst>(i);
            for (auto imp : c->improvements)
            {
                if (dye_plants.count(imp->mat_index) && dye_plants.at(imp->mat_index) == imp->mat_type)
                {
                    return false;
                }
            }
            return true;
        });
    }
    case stock_item::clothes_feet:
    {
        return find_item_helper_clothes(items_other_id::SHOES);
    }
    case stock_item::clothes_hands:
    {
        return find_item_helper_clothes(items_other_id::GLOVES);
    }
    case stock_item::clothes_head:
    {
        return find_item_helper_clothes(items_other_id::HELM);
    }
    case stock_item::clothes_legs:
    {
        return find_item_helper_clothes(items_other_id::PANTS);
    }
    case stock_item::clothes_torso:
    {
        return find_item_helper_clothes(items_other_id::ARMOR);
    }
    case stock_item::coal:
    {
        return std::make_pair(items_other_id::BAR, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "COAL";
        });
    }
    case stock_item::coffin:
    {
        return std::make_pair(items_other_id::COFFIN, yes_i_mean_all);
    }
    case stock_item::crossbow:
    {
        return std::make_pair(items_other_id::WEAPON, [this](df::item *i) -> bool
        {
            return virtual_cast<df::item_weaponst>(i)->subtype->subtype == manager_subtype.at(stock_item::crossbow);
        });
    }
    case stock_item::crutch:
    {
        return std::make_pair(items_other_id::CRUTCH, yes_i_mean_all);
    }
    case stock_item::door:
    {
        return std::make_pair(items_other_id::DOOR, yes_i_mean_all);
    }
    case stock_item::drink:
    {
        return std::make_pair(items_other_id::DRINK, yes_i_mean_all);
    }
    case stock_item::drink_fruit:
    {
        return std::make_pair(items_other_id::PLANT_GROWTH, [this](df::item *i) -> bool
        {
            return drink_fruits.count(i->getMaterialIndex()) && drink_fruits.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::drink_plant:
    {
        return std::make_pair(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return drink_plants.count(i->getMaterialIndex()) && drink_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::dye:
    {
        return std::make_pair(items_other_id::POWDER_MISC, [this](df::item *i) -> bool
        {
            return dye_plants.count(i->getMaterialIndex()) && dye_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::dye_plant:
    {
        return std::make_pair(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return mill_plants.count(i->getMaterialIndex()) && mill_plants.at(i->getMaterialIndex()) == i->getMaterial() && dye_plants.count(i->getMaterialIndex());
        });
        break;
    }
    case stock_item::dye_seeds:
    {
        return std::make_pair(items_other_id::SEEDS, [this](df::item *i) -> bool
        {
            return dye_plants.count(i->getMaterialIndex()) && grow_plants.count(i->getMaterialIndex());
        });
    }
    case stock_item::flask:
    {
        return std::make_pair(items_other_id::FLASK, yes_i_mean_all);
    }
    case stock_item::floodgate:
    {
        return std::make_pair(items_other_id::FLOODGATE, yes_i_mean_all);
    }
    case stock_item::food_ingredients:
    {
        std::set<std::tuple<df::item_type, int16_t, int16_t, int32_t>> forbidden;
        for (size_t i = 0; i < ui->kitchen.item_types.size(); i++)
        {
            if ((ui->kitchen.exc_types[i] & 1) == 1)
            {
                forbidden.insert(std::make_tuple(ui->kitchen.item_types[i], ui->kitchen.item_subtypes[i], ui->kitchen.mat_types[i], ui->kitchen.mat_indices[i]));
            }
        }

        // TODO: count stacks, not items
        return std::make_pair(items_other_id::ANY_COOKABLE, [forbidden](df::item *i) -> bool
        {
            if (virtual_cast<df::item_flaskst>(i))
                return false;
            if (virtual_cast<df::item_cagest>(i))
                return false;
            if (virtual_cast<df::item_barrelst>(i))
                return false;
            if (virtual_cast<df::item_bucketst>(i))
                return false;
            if (virtual_cast<df::item_animaltrapst>(i))
                return false;
            if (virtual_cast<df::item_boxst>(i))
                return false;
            if (virtual_cast<df::item_toolst>(i))
                return false;
            return !forbidden.count(std::make_tuple(i->getType(), i->getSubtype(), i->getMaterial(), i->getMaterialIndex()));
        });
    }
    case stock_item::giant_corkscrew:
    {
        return std::make_pair(items_other_id::TRAPCOMP, [this](df::item *item) -> bool
        {
            df::item_trapcompst *i = virtual_cast<df::item_trapcompst>(item);
            return i && i->subtype->subtype == manager_subtype.at(stock_item::giant_corkscrew);
        });
    }
    case stock_item::goblet:
    {
        return std::make_pair(items_other_id::GOBLET, yes_i_mean_all);
    }
    case stock_item::gypsum:
    {
        return std::make_pair(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return is_gypsum(i);
        });
    }
    case stock_item::hatch_cover:
    {
        return std::make_pair(items_other_id::HATCH_COVER, yes_i_mean_all);
    }
    case stock_item::hive:
    {
        return find_item_helper_tool(stock_item::hive);
    }
    case stock_item::honey:
    {
        MaterialInfo honey;
        honey.findCreature("HONEY_BEE", "HONEY");

        return std::make_pair(items_other_id::LIQUID_MISC, [honey](df::item *i) -> bool
        {
            return i->getMaterialIndex() == honey.index && i->getMaterial() == honey.type;
        });
    }
    case stock_item::honeycomb:
    {
        return find_item_helper_tool(stock_item::honeycomb);
    }
    case stock_item::jug:
    {
        return find_item_helper_tool(stock_item::jug);
    }
    case stock_item::leather:
    {
        return std::make_pair(items_other_id::SKIN_TANNED, yes_i_mean_all);
    }
    case stock_item::lye:
    {
        return std::make_pair(items_other_id::LIQUID_MISC, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "LYE";
        });
    }
    case stock_item::meal:
    {
        return std::make_pair(items_other_id::ANY_GOOD_FOOD, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_foodst>(i);
        });
    }
    case stock_item::mechanism:
    {
        return std::make_pair(items_other_id::TRAPPARTS, yes_i_mean_all);
    }
    case stock_item::metal_ore:
    {
        return std::make_pair(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return is_metal_ore(i);
        });
    }
    case stock_item::milk:
    {
        return std::make_pair(items_other_id::LIQUID_MISC, [this](df::item *i) -> bool
        {
            return milk_creatures.count(i->getMaterialIndex()) && milk_creatures.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::mill_plant:
    {
        return std::make_pair(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return mill_plants.count(i->getMaterialIndex()) && mill_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::minecart:
    {
        return find_item_helper_tool(stock_item::minecart);
    }
    case stock_item::nest_box:
    {
        return find_item_helper_tool(stock_item::nest_box);
    }
    case stock_item::paper:
    {
        return std::make_pair(items_other_id::SHEET, yes_i_mean_all);
    }
    case stock_item::pick:
    {
        return find_item_helper_weapon(job_skill::MINING);
    }
    case stock_item::pipe_section:
    {
        return std::make_pair(items_other_id::PIPE_SECTION, yes_i_mean_all);
    }
    case stock_item::plaster_powder:
    {
        return std::make_pair(items_other_id::POWDER_MISC, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "PLASTER";
        });
    }
    case stock_item::quern:
    {
        // TODO: include used in building
        return std::make_pair(items_other_id::QUERN, yes_i_mean_all);
    }
    case stock_item::quire:
    {
        return find_item_helper_tool(stock_item::quire);
    }
    case stock_item::quiver:
    {
        return std::make_pair(items_other_id::QUIVER, yes_i_mean_all);
    }
    case stock_item::raw_adamantine:
    {
        return std::make_pair(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return i->getMaterialIndex() == manager_subtype.at(stock_item::raw_adamantine);
        });
    }
    case stock_item::raw_coke:
    {
        return std::make_pair(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return !is_raw_coke(i).empty();
        });
    }
    case stock_item::raw_fish:
    {
        return std::make_pair(items_other_id::FISH_RAW, yes_i_mean_all);
    }
    case stock_item::rock_pot:
    {
        return find_item_helper_tool(stock_item::rock_pot);
    }
    case stock_item::rope:
    {
        return std::make_pair(items_other_id::CHAIN, yes_i_mean_all);
    }
    case stock_item::rough_gem:
    {
        return std::make_pair(items_other_id::ROUGH, [](df::item *i) -> bool
        {
            return i->getMaterial() == 0;
        });
    }
    case stock_item::shell:
    {
        return std::make_pair(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            return i->corpse_flags.bits.shell && !i->corpse_flags.bits.unbutchered;
        });
    }
    case stock_item::skull:
    {
        // XXX exclude dwarf skulls ?
        return std::make_pair(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            return i->corpse_flags.bits.skull && !i->corpse_flags.bits.unbutchered;
        });
    }
    case stock_item::slab:
    {
        return std::make_pair(items_other_id::SLAB, [](df::item *i) -> bool
        {
            return i->getSlabEngravingType() == slab_engraving_type::Slab;
        });
    }
    case stock_item::slurry:
    {
        return std::make_pair(items_other_id::GLOB, [](df::item *i) -> bool
        {
            if (!virtual_cast<df::item_globst>(i)->mat_state.bits.paste)
            {
                return false;
            }

            MaterialInfo mat(i);
            for (auto it : mat.material->reaction_class)
            {
                if (*it == "PAPER_SLURRY")
                {
                    return true;
                }
            }

            return false;
        });
    }
    case stock_item::slurry_plant:
    {
        return std::make_pair(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return slurry_plants.count(i->getMaterialIndex()) && slurry_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::soap:
    {
        return std::make_pair(items_other_id::BAR, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "SOAP";
        });
    }
    case stock_item::splint:
    {
        return std::make_pair(items_other_id::SPLINT, yes_i_mean_all);
    }
    case stock_item::statue:
    {
        return std::make_pair(items_other_id::STATUE, yes_i_mean_all);
    }
    case stock_item::stepladder:
    {
        return find_item_helper_tool(stock_item::stepladder);
    }
    case stock_item::stone:
    {
        return std::make_pair(items_other_id::BOULDER, [](df::item *i) -> bool
        {
            return !ui->economic_stone[i->getMaterialIndex()];
        });
    }
    case stock_item::table:
    {
        return std::make_pair(items_other_id::TABLE, yes_i_mean_all);
    }
    case stock_item::tallow:
    {
        return std::make_pair(items_other_id::GLOB, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "TALLOW";
        });
    }
    case stock_item::thread:
    {
        return std::make_pair(items_other_id::THREAD, [](df::item *i) -> bool
        {
            return !i->flags.bits.spider_web && virtual_cast<df::item_threadst>(i)->dimension == 15000;
        });
    }
    case stock_item::thread_plant:
    {
        return std::make_pair(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return thread_plants.count(i->getMaterialIndex()) && thread_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::thread_seeds:
    {
        return std::make_pair(items_other_id::SEEDS, [this](df::item *i) -> bool
        {
            return thread_plants.count(i->getMaterialIndex()) && grow_plants.count(i->getMaterialIndex());
        });
    }
    case stock_item::toy:
    {
        return std::make_pair(items_other_id::TOY, yes_i_mean_all);
    }
    case stock_item::traction_bench:
    {
        return std::make_pair(items_other_id::TRACTION_BENCH, yes_i_mean_all);
    }
    case stock_item::training_weapon:
    {
        return find_item_helper_weapon(job_skill::NONE, true);
    }
    case stock_item::weapon:
    {
        return find_item_helper_weapon();
    }
    case stock_item::weapon_rack:
    {
        return std::make_pair(items_other_id::WEAPONRACK, yes_i_mean_all);
    }
    case stock_item::wheelbarrow:
    {
        return find_item_helper_tool(stock_item::wheelbarrow);
    }
    case stock_item::wood:
    {
        return std::make_pair(items_other_id::WOOD, yes_i_mean_all);
    }
    case stock_item::wool:
    {
        // used for SpinThread which currently ignores the material_amount
        // note: if it didn't, use either HairWool or Yarn but not both
        return std::make_pair(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            return i->corpse_flags.bits.hair_wool || i->corpse_flags.bits.yarn;
        });
    }
    case stock_item::written_on_quire:
    {
        return std::make_pair(items_other_id::TOOL, [this](df::item *i) -> bool
        {
            return i->getSubtype() == manager_subtype.at(stock_item::quire) && i->hasSpecificImprovements(improvement_type::WRITING);
        });
    }
    case stock_item::_stock_item_count:
    {
        break;
    }
    }

    throw DFHack::Error::InvalidArgument();
}

std::pair<df::items_other_id, std::function<bool(df::item *)>> Stocks::find_item_helper_weapon(df::job_skill skill, bool training)
{
    // TODO: count minimum subtype
    return std::make_pair(items_other_id::WEAPON, [skill, training](df::item *item) -> bool
    {
        df::item_weaponst *i = virtual_cast<df::item_weaponst>(item);

        auto & ue = ui->main.fortress_entity->entity_raw->equipment;
        if (std::find(ue.digger_id.begin(), ue.digger_id.end(), i->subtype->subtype) == ue.digger_id.end() &&
            std::find(ue.weapon_id.begin(), ue.weapon_id.end(), i->subtype->subtype) == ue.weapon_id.end())
        {
            return false;
        }

        if (skill != job_skill::NONE && i->subtype->skill_melee != skill)
        {
            return false;
        }

        if (i->subtype->flags.is_set(weapon_flags::TRAINING) != training)
        {
            return false;
        }

        return true;
    });
}

template<typename I>
static bool is_armor_metal(I *i) { return i->subtype->props.flags.is_set(armor_general_flags::METAL); }

template<typename I>
static std::pair<df::items_other_id, std::function<bool(df::item *)>> find_item_helper_armor_helper(df::items_other_id oidx, const std::vector<int16_t> & idefs, std::function<bool(I *)> pred = is_armor_metal<I>)
{
    // TODO: count minimum subtype
    return std::make_pair(oidx, [idefs, pred](df::item *item) -> bool
    {
        I *i = virtual_cast<I>(item);
        if (std::find(idefs.begin(), idefs.end(), i->subtype->subtype) == idefs.end())
        {
            return false;
        }

        if (!pred(i))
        {
            return false;
        }

        return true;
    });
}

std::pair<df::items_other_id, std::function<bool(df::item *)>> Stocks::find_item_helper_armor(df::items_other_id oidx)
{
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    switch (oidx)
    {
    case items_other_id::ARMOR:
        return find_item_helper_armor_helper<df::item_armorst>(oidx, ue.armor_id);
    case items_other_id::SHIELD:
        return find_item_helper_armor_helper<df::item_shieldst>(oidx, ue.shield_id, [](df::item_shieldst *) -> bool { return true; });
    case items_other_id::HELM:
        return find_item_helper_armor_helper<df::item_helmst>(oidx, ue.helm_id);
    case items_other_id::PANTS:
        return find_item_helper_armor_helper<df::item_pantsst>(oidx, ue.pants_id);
    case items_other_id::GLOVES:
        return find_item_helper_armor_helper<df::item_glovesst>(oidx, ue.gloves_id); // TODO: divide count by 2
    case items_other_id::SHOES:
        return find_item_helper_armor_helper<df::item_shoesst>(oidx, ue.shoes_id); // TODO: divide count by 2
    default:
        throw DFHack::Error::InvalidArgument();
    }
}

template<typename I>
static std::pair<df::items_other_id, std::function<bool(df::item *)>> find_item_helper_clothes_helper(df::items_other_id oidx, const std::vector<int16_t> & idefs)
{
    // TODO: count minimum subtype
    return std::make_pair(oidx, [idefs](df::item *item) -> bool
    {
        I *i = virtual_cast<I>(item);
        if (!i->subtype->props.flags.is_set(armor_general_flags::SOFT)) // XXX
        {
            return false;
        }

        if (std::find(idefs.begin(), idefs.end(), i->subtype->subtype) == idefs.end())
        {
            return false;
        }

        return i->mat_type != 0 && // XXX
            i->wear == 0;
    });
}

std::pair<df::items_other_id, std::function<bool(df::item *)>> Stocks::find_item_helper_clothes(df::items_other_id oidx)
{
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    switch (oidx)
    {
    case items_other_id::ARMOR:
        return find_item_helper_clothes_helper<df::item_armorst>(oidx, ue.armor_id);
    case items_other_id::HELM:
        return find_item_helper_clothes_helper<df::item_helmst>(oidx, ue.helm_id);
    case items_other_id::PANTS:
        return find_item_helper_clothes_helper<df::item_pantsst>(oidx, ue.pants_id);
    case items_other_id::GLOVES:
        return find_item_helper_clothes_helper<df::item_glovesst>(oidx, ue.gloves_id); // TODO: divide by 2
    case items_other_id::SHOES:
        return find_item_helper_clothes_helper<df::item_shoesst>(oidx, ue.shoes_id); // TODO: divide by 2
    default:
        throw DFHack::Error::InvalidArgument();
    }
}

std::pair<df::items_other_id, std::function<bool(df::item *)>> Stocks::find_item_helper_tool(stock_item::item k)
{
    return std::make_pair(items_other_id::TOOL, [this, k](df::item *item) -> bool
    {
        df::item_toolst *i = virtual_cast<df::item_toolst>(item);
        return i->subtype->subtype == manager_subtype.at(k) &&
            i->stockpile.id == -1 &&
            (i->vehicle_id == -1 || df::vehicle::find(i->vehicle_id)->route_id == -1);
    });
}
