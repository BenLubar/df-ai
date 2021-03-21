#include "ai.h"
#include "stocks.h"

#include "modules/Maps.h"

#include "df/building_farmplotst.h"
#include "df/item_seedsst.h"
#include "df/job.h"
#include "df/plant.h"
#include "df/tile_designation.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

void Stocks::count_seeds(color_ostream &)
{
    farmplots.clear();
    ai.find_room(room_type::farmplot, [this](room *r) -> bool
    {
        df::building_farmplotst *bld = virtual_cast<df::building_farmplotst>(r->dfbuilding());
        if (!bld)
            return false;

        for (uint8_t season = 0; season < 4; season++)
        {
            farmplots[std::make_pair(season, bld->plant_id[season])]++;
        }

        return false; // search all farm plots
    });
    seeds.clear();
    for (auto i : world->items.other[items_other_id::SEEDS])
    {
        df::item_seedsst *s = virtual_cast<df::item_seedsst>(i);
        if (s && is_item_free(s))
        {
            seeds[s->mat_index] += s->stack_size;
        }
    }
    updating_seeds = false;
}

void Stocks::farmplot(color_ostream & out, room *r, bool initial)
{
    df::building_farmplotst *bld = virtual_cast<df::building_farmplotst>(r->dfbuilding());
    if (!bld)
        return;

    bool subterranean = Maps::getTileDesignation(r->pos())->bits.subterranean;
    df::coord2d region(Maps::getTileBiomeRgn(r->pos()));
    df::biome_type biome = subterranean ? biome_type::SUBTERRANEAN_WATER : Maps::GetBiomeType(region.x, region.y);
    df::plant_raw_flags plant_biome;
    if (!find_enum_item(&plant_biome, "BIOME_" + enum_item_key(biome)))
    {
        ai.debug(out, "[ERROR] stocks: could not find plant raw flag for biome: " + enum_item_key(biome));
        return;
    }

    std::vector<int32_t> may;
    for (int32_t i = 0; i < int32_t(world->raws.plants.all.size()); i++)
    {
        df::plant_raw *p = world->raws.plants.all[i];
        if (!p->flags.is_set(plant_biome))
            continue;
        if (p->flags.is_set(plant_raw_flags::TREE) || !p->flags.is_set(plant_raw_flags::SEED))
            continue;
        may.push_back(i);
    }

    bld->farm_flags.bits.seasonal_fertilize = true;

    bool isfirst = ai.find_room(room_type::farmplot, [&](room *other) -> bool { return r->farm_type == other->farm_type && r->outdoor == other->outdoor; }) == r;
    for (int8_t season = 0; season < 4; season++)
    {
        std::vector<int32_t> pids;
        if (r->farm_type == farm_type::food)
        {
            for (auto i = may.begin(); i != may.end(); i++)
            {
                df::plant_raw *p = world->raws.plants.all[*i];

                // season numbers are also the 1st 4 flags
                if (!p->flags.is_set(df::plant_raw_flags(season)))
                {
                    continue;
                }

                MaterialInfo pm(p->material_defs.type[plant_material_def::basic_mat], p->material_defs.idx[plant_material_def::basic_mat]);
                if (isfirst)
                {
                    if (pm.material->flags.is_set(material_flags::EDIBLE_RAW) && p->flags.is_set(plant_raw_flags::DRINK))
                    {
                        pids.push_back(*i);
                    }
                    continue;
                }
                if (pm.material->flags.is_set(material_flags::EDIBLE_RAW) || pm.material->flags.is_set(material_flags::EDIBLE_COOKED) || p->flags.is_set(plant_raw_flags::DRINK))
                {
                    pids.push_back(*i);
                    continue;
                }
                if (p->flags.is_set(plant_raw_flags::MILL))
                {
                    MaterialInfo mm(p->material_defs.type[plant_material_def::mill], p->material_defs.idx[plant_material_def::mill]);
                    if (mm.material->flags.is_set(material_flags::EDIBLE_RAW) || mm.material->flags.is_set(material_flags::EDIBLE_COOKED))
                    {
                        pids.push_back(*i);
                        continue;
                    }
                }
                for (size_t bi = 0; bi < pm.material->reaction_product.id.size(); bi++)
                {
                    if (*pm.material->reaction_product.id[bi] == "BAG_ITEM")
                    {
                        MaterialInfo bm(pm.material->reaction_product.material.mat_type[bi], pm.material->reaction_product.material.mat_index[bi]);
                        if (bm.material->flags.is_set(material_flags::EDIBLE_RAW) || bm.material->flags.is_set(material_flags::EDIBLE_COOKED))
                        {
                            pids.push_back(*i);
                            break;
                        }
                    }
                }
            }
        }
        else if (r->farm_type == farm_type::cloth)
        {
            if (isfirst)
            {
                for (auto i = may.begin(); i != may.end(); i++)
                {
                    df::plant_raw *p = world->raws.plants.all[*i];
                    if (p->flags.is_set(df::plant_raw_flags(season)) && thread_plants.count(*i))
                    {
                        pids.push_back(*i);
                    }
                }
            }
            // only grow dyes the first field if there is no cloth crop available
            if (pids.empty())
            {
                for (auto i = may.begin(); i != may.end(); i++)
                {
                    df::plant_raw *p = world->raws.plants.all[*i];
                    if (p->flags.is_set(df::plant_raw_flags(season)) && (thread_plants.count(*i) || dye_plants.count(*i)))
                    {
                        pids.push_back(*i);
                    }
                }
            }
        }
        std::sort(pids.begin(), pids.end(), [this, season](int32_t a, int32_t b) -> bool
        {
            if (seeds.count(a) && !seeds.count(b))
                return true;
            if (!seeds.count(a) && seeds.count(b))
                return false;
            int32_t ascore = plants.count(a) ? int32_t(plants.at(a)) : 0;
            int32_t bscore = plants.count(b) ? int32_t(plants.at(b)) : 0;
            if (seeds.count(a))
            {
                ascore -= int32_t(seeds.at(a));
                bscore -= int32_t(seeds.at(b));
            }
            ascore += farmplots.count(std::make_pair(season, a)) ? 3 * 3 * 2 * int32_t(farmplots.at(std::make_pair(season, a))) : 0;
            bscore += farmplots.count(std::make_pair(season, b)) ? 3 * 3 * 2 * int32_t(farmplots.at(std::make_pair(season, b))) : 0;
            return ascore < bscore;
        });

        if (pids.empty())
        {
            std::ostringstream str;
            str << r->farm_type;
            if (!isfirst && complained_about_no_plants.insert(std::make_tuple(r->farm_type, biome, season)).second)
            {
                ai.debug(out, stl_sprintf("[ERROR] stocks: no legal plants for %s farm plot (%s) for season %d", str.str().c_str(), enum_item_key_str(biome), season));
            }
        }
        else
        {
            if (!initial)
            {
                farmplots[std::make_pair(season, bld->plant_id[season])]--;
                farmplots[std::make_pair(season, pids[0])]++;
            }
            bld->plant_id[season] = pids[0];
        }
    }
}

// designate some trees for woodcutting
df::coord Stocks::cuttrees(color_ostream &, int32_t amount, std::ostream & reason)
{
    std::set<df::coord> jobs;

    for (auto job = world->jobs.list.next; job; job = job->next)
    {
        if (job->item->job_type == job_type::FellTree)
        {
            jobs.insert(job->item->pos);
        }
    }

    if (last_cutpos.isValid() && (Maps::getTileDesignation(last_cutpos)->bits.dig != tile_dig_designation::No || jobs.count(last_cutpos)) && cut_wait_counter < amount * 10)
    {
        // skip designating if we haven't cut the last tree yet
        reason << "waiting for trees to be cut: " << jobs.size() << " remaining";
        cut_wait_counter++;
        return last_cutpos;
    }
    cut_wait_counter = 0;

    // return the bottom-rightest designated tree
    df::coord br;
    br.clear();

    size_t designated_count = 0;

    auto list = tree_list();

    for (auto tree : list)
    {
        if (ENUM_ATTR(tiletype, material, *Maps::getTileType(tree)) != tiletype_material::TREE)
        {
            continue;
        }

        if (!br.isValid() || (br.x & -16) < (tree.x & -16) || ((br.x & -16) == (tree.x & -16) && (br.y & -16) < (tree.y & -16)))
        {
            br = tree;
        }

        if (Maps::getTileDesignation(tree)->bits.dig == tile_dig_designation::No && !jobs.count(tree))
        {
            designated_count++;
            AI::dig_tile(tree, tile_dig_designation::Default);
        }

        amount--;
        if (amount <= 0)
        {
            break;
        }
    }

    reason << "marked " << designated_count << " trees for cutting";
    if (!jobs.empty())
    {
        reason << "; " << jobs.size() << " trees already marked";
    }

    return br;
}

// return a list of trees on the map
// lists only visible trees, sorted by distance from the fort entrance
// expensive method, dont call often
std::set<df::coord, std::function<bool(df::coord, df::coord)>> Stocks::tree_list()
{
    uint16_t walkable = Maps::getTileWalkable(ai.fort_entrance_pos());

    auto is_walkable = [walkable](df::coord t) -> bool
    {
        return walkable == Maps::getTileWalkable(t);
    };

    auto add_from_vector = [this, is_walkable](std::vector<df::plant *> & trees)
    {
        for (auto it = trees.begin(); it != trees.end(); it++)
        {
            df::plant *p = *it;
            df::tiletype tt = *Maps::getTileType(p->pos);
            if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE &&
                ENUM_ATTR(tiletype, shape, tt) == tiletype_shape::WALL &&
                !Maps::getTileDesignation(p->pos)->bits.hidden &&
                !AI::spiral_search(p->pos, 1, [](df::coord t) -> bool
            {
                df::tile_designation *td = Maps::getTileDesignation(t);
                return td && td->bits.flow_size > 0;
            }).isValid() &&
                AI::spiral_search(p->pos, 1, is_walkable).isValid())
            {
                last_treelist.insert(p->pos);
            }
        }
    };

    last_treelist.clear();
    add_from_vector(world->plants.tree_dry);
    add_from_vector(world->plants.tree_wet);

    return last_treelist;
}
