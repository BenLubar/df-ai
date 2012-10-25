class DwarfAI
    class Stocks
        Needed = { :bin => 6, :barrel => 6, :bucket => 4, :bag => 4,
            :food => 20, :drink => 20, :soap => 5, :logs => 10, :coal => 5,
            :pigtail_seeds => 10, :dimplecup_seeds => 10, :dimple_dye => 10,
            :splint => 2, :crutch => 2, :rockblock => 1,
        }
        NeededPerDwarf = { :food => 1, :drink => 2 }

        WatchStock = { :roughgem => 6, :pigtail => 10, :cloth_nodye => 10,
            :metal_ore => 6, :raw_coke => 2,
            :quarrybush => 4, :skull => 2, :bone => 8, :leaves => 5 }

        attr_accessor :ai, :count
        attr_accessor :onupdate_handle
        def initialize(ai)
            @ai = ai
            @updating = []
            @count = {}
        end

        def startup
        end

        def onupdate_register
            @onupdate_handle = df.onupdate_register(2400, 30) { update }
        end

        def onupdate_unregister
            df.onupdate_unregister(@onupdate_handle)
        end

        def update
            return unless @updating.empty?

            @updating = Needed.keys | WatchStock.keys
            @updating_count = @updating.dup
            puts 'AI: updating stocks' if $DEBUG

            # do stocks accounting 'in the background' (ie one bit at a time)
            df.onupdate_register_once(8) {
                if key = @updating_count.shift
                    @count[key] = count_stocks(key)
                    false
                elsif key = @updating.shift
                    act(key)
                    false
                else
                    # finished, dismiss callback
                    true
                end
            }
        end

        def act(key)
            if amount = Needed[key]
                amount += ai.pop.citizen.length * NeededPerDwarf[key].to_i
                queue_need(key, (amount-@count[key])*3/2) if @count[key] < amount
            end
            
            if amount = WatchStock[key]
                queue_use(key, @count[key]-amount) if @count[key] > amount
            end
        end

        def count_stocks(k)
            case k
            when :bin
                df.world.items.other[:BIN].find_all { |i| i.stockpile.id == -1 }
            when :barrel
                df.world.items.other[:BARREL].find_all { |i| i.stockpile.id == -1 }
            when :bag
                df.world.items.other[:BOX].find_all { |i| df.decode_mat(i).plant }
            when :bucket
                df.world.items.other[:BUCKET]
            when :food
                df.world.items.other[:ANY_GOOD_FOOD].reject { |i|
                    case i
                    when DFHack::ItemSeedsst, DFHack::ItemBoxst, DFHack::ItemFishRawst
                        true
                    end
                }
            when :drink
                df.world.items.other[:DRINK]
            when :soap, :coal
                mat_id = {:soap => 'SOAP', :coal => 'COAL'}[k]
                df.world.items.other[:BAR].find_all { |i|
                    mat = df.decode_mat(i) and mat.material and mat.material.id == mat_id
                }
            when :logs
                df.world.items.other[:WOOD]
            when :roughgem
                df.world.items.other[:ROUGH]
            when :metal_ore
                df.world.items.other[:BOULDER].find_all { |i| is_metal_ore(i) }
            when :raw_coke
                df.world.items.other[:BOULDER].find_all { |i| is_raw_coke(i) }
            when :splint
                df.world.items.other[:SPLINT].reject { |i| i.flags.in_inventory and
                    i.itemrefs.grep(DFHack::GeneralRefUnitHolderst).find { |r|
                        r.unit_tg.inventory.find { |ii| ii.item == i and ii.mode != :Hauled } } }
            when :crutch
                df.world.items.other[:CRUTCH].reject { |i| i.flags.in_inventory and
                    i.itemrefs.grep(DFHack::GeneralRefUnitHolderst).find { |r|
                        r.unit_tg.inventory.find { |ii| ii.item == i and ii.mode != :Hauled } } }
            when :crossbow
                df.world.items.other[:WEAPON].find_all { |i|
                    i.subtype.subtype == ai.plan.class::ManagerSubtype[:MakeBoneCrossbow]
                }
            when :pigtail, :dimplecup, :quarrybush
                # TODO generic handling, same as farm crops selection
                mspec = {
                    :pigtail => 'PLANT:GRASS_TAIL_PIG:STRUCTURAL',
                    :dimplecup => 'PLANT:MUSHROOM_CUP_DIMPLE:STRUCTURAL',
                    :quarrybush => 'PLANT:BUSH_QUARRY:STRUCTURAL',
                }[k]
                df.world.items.other[:PLANT].grep(df.decode_mat(mspec))
            when :pigtail_seeds, :dimplecup_seeds
                mspec = {
                    :pigtail_seeds => 'PLANT:GRASS_TAIL_PIG:SEED',
                    :dimplecup_seeds => 'PLANT:MUSHROOM_CUP_DIMPLE:SEED',
                }[k]
                df.world.items.other[:SEEDS].grep(df.decode_mat(mspec))
            when :dimple_dye
                mspec = 'PLANT:MUSHROOM_CUP_DIMPLE:MILL'
                df.world.items.other[:POWDER_MISC].grep(df.decode_mat(mspec))
            when :leaves
                df.world.items.other[:LEAVES]
            when :rockblock
                df.world.items.other[:BLOCKS]
            when :skull
                # XXX exclude dwarf skulls ?
                df.world.items.other[:CORPSEPIECE].find_all { |i|
                    i.corpse_flags.skull
                }
            when :bone
                return df.world.items.other[:CORPSEPIECE].find_all { |i|
                    i.corpse_flags.bone
                }.inject(0) { |s, i|
                    # corpsepieces uses this instead of i.stack_size
                    s + i.material_amount[:Bone]
                }
            when :cloth_nodye
                df.world.items.other[:CLOTH].find_all { |i|
                    !i.flags.in_chest and   # infirmary..
                    !i.improvements.find { |imp| imp.dye.mat_type != -1 }
                }
            else
                return Needed[k] ? 1000000 : -1

            end.find_all { |i|
                is_item_free(i)
            }.inject(0) { |s, i| s + i.stack_size }
        end

        def queue_need(what, amount)
            case what
            when :soap
                return if ai.plan.rooms.find { |r| r.type == :infirmary and r.status != :finished }

            when :food
                # XXX fish/hunt/cook ?
                @last_warn_food ||= Time.now-610    # warn every 10mn
                if @last_warn_food < Time.now-600
                    puts "AI: need #{amount} more food"
                    @last_warn_food = Time.now
                end
                return

            when :pigtail_seeds, :dimplecup_seeds
                # only useful at game start, with low seeds stocks
                input = {
                    :pigtail_seeds => :pigtail,
                    :dimplecup_seeds => :dimplecup,
                }[what]

                reaction = {
                    :pigtail_seeds => :ProcessPlants,
                    :dimplecup_seeds => :MillPlants,
                }[what]

            when :dimple_dye
                reaction = :MillPlants
                input = :dimplecup

            when :logs
                tree_list.each { |t| amount -= 1 if df.map_tile_at(t).designation.dig == :Default }
                cuttrees(amount) if amount > 0
                return

            when :drink
                amount = (amount+4)/5  # accounts for brewer yield, but not for input stack size

            when :rockblock
                amount = (amount+3)/4

            end

            if input
                n_input = count[input] || count_stocks(input)
                amount = n_input if amount > n_input
            end

            return if amount <= 0

            amount = 30 if amount > 30

            reaction ||= DwarfAI::Plan::FurnitureOrder[what]
            ai.plan.find_manager_orders(reaction).each { |o| amount -= o.amount_total }
            return if amount <= 0

            puts "AI: stocks: queue #{amount} #{reaction}" if $DEBUG
            ai.plan.add_manager_order(reaction, amount)
        end

        def queue_use(what, amount)
            case what
            when :metal_ore
                queue_use_metal_ore(amount)
                return

            when :raw_coke
                queue_use_raw_coke(amount)
                return

            when :roughgem
                queue_use_gems(amount)
                return

            when :pigtail, :dimplecup, :quarrybush
                reaction = {
                    :pigtail => :ProcessPlants,
                    :dimplecup => :MillPlants,
                    :quarrybush => :ProcessPlantsBag,
                }[what]
                # stuff may rot before we can process it
                amount = 20 if amount > 20

            when :leaves
                reaction = :PrepareMeal
                amount = (amount / 5.0).ceil

            when :skull
                reaction = :MakeTotem

            when :bone
                nhunters = ai.pop.labor_worker[:HUNT].length if ai.pop.labor_worker
                return if not nhunters
                need_crossbow = nhunters + 1 - count_stocks(:crossbow)
                if need_crossbow > 0
                    reaction = :MakeBoneCrossbow
                    amount = need_crossbow if amount > need_crossbow
                else
                    reaction = :MakeBoneBolt
                    # seems like we consume more than we think..
                    amount /= 2
                end

            when :cloth_nodye
                reaction = :DyeCloth
                input = :dimple_dye
                # cloth may already be queued for sewing
                amount = 10 if amount > 10

            end

            ai.plan.find_manager_orders(reaction).each { |o| amount -= o.amount_total }

            return if amount <= 0

            amount = 30 if amount > 30

            puts "AI: stocks: queue #{amount} #{reaction}" if $DEBUG
            ai.plan.add_manager_order(reaction, amount)
        end

        def queue_use_gems(amount)
            return if df.world.manager_orders.find { |mo| mo.job_type == :CutGems }

            i = df.world.items.other[:ROUGH].find { |_i| is_item_free(_i) }
            this_amount = df.world.items.other[:ROUGH].find_all { |_i|
                _i.mat_type == i.mat_type and _i.mat_index == i.mat_index and is_item_free(_i)
            }.length
            amount = this_amount if this_amount < amount
            amount = amount*3/4 if amount >= 10
            amount = 30 if amount > 30

            puts "AI: stocks: queue #{amount} CutGems #{df.decode_mat(i)}" if $DEBUG
            ai.plan.ensure_workshop(:Jewelers, false)
            df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :CutGems, :unk_2 => -1, :item_subtype => -1,
                    :mat_type => i.mat_type, :mat_index => i.mat_index, :amount_left => amount, :amount_total => amount)
        end

        def queue_use_metal_ore(amount)
            # TODO actively dig metal ore veins
            return if df.world.manager_orders.find { |mo| mo.job_type == :SmeltOre }

            i = df.world.items.other[:BOULDER].find { |_i| is_metal_ore(_i) and is_item_free(_i) }
            this_amount = df.world.items.other[:BOULDER].find_all { |_i|
                _i.mat_type == i.mat_type and _i.mat_index == i.mat_index and is_item_free(_i)
            }.length
            amount = this_amount if this_amount < amount
            amount = amount*3/4 if amount >= 10
            amount = 30 if amount > 30

            if df.world.buildings.other[:FURNACE_SMELTER_MAGMA].empty?
                amount = @count[:coal] if amount > @count[:coal]
                return if amount <= 0
            end

            puts "AI: stocks: queue #{amount} SmeltOre #{df.decode_mat(i)}" if $DEBUG
            ai.plan.ensure_workshop(:Smelter, false)
            df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :SmeltOre, :unk_2 => -1, :item_subtype => -1,
                    :mat_type => i.mat_type, :mat_index => i.mat_index, :amount_left => amount, :amount_total => amount)
        end

        def queue_use_raw_coke(amount)
            is_raw_coke(nil)    # populate @raw_coke_cache
            inv = @raw_coke_cache.invert
            return if df.world.manager_orders.find { |mo| mo.job_type == :CustomReaction and inv[mo.reaction_name] }

            i = df.world.items.other[:BOULDER].find { |_i| is_raw_coke(_i) and is_item_free(_i) }
            return if not i or not reaction = is_raw_coke(i)

            this_amount = df.world.items.other[:BOULDER].find_all { |_i|
                _i.mat_index == i.mat_index and is_item_free(_i)
            }.length
            amount = this_amount if this_amount < amount
            amount = amount*3/4 if amount >= 10
            amount = 30 if amount > 30

            if df.world.buildings.other[:FURNACE_SMELTER_MAGMA].empty?
                # need at least 1 unit of fuel to bootstrap
                return if @count[:coal] <= 0
            end

            puts "AI: stocks: queue #{amount} #{reaction}" if $DEBUG
            ai.plan.ensure_workshop(:Smelter, false)
            df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :CustomReaction, :unk_2 => -1, :item_subtype => -1,
                    :mat_type => -1, :mat_index => -1, :amount_left => amount, :amount_total => amount, :reaction_name => reaction)
        end

        # designate some trees to cut
        def cuttrees(amount)
            list = tree_list
            list.each { |tree|
                t = df.map_tile_at(tree)
                next if t.shape != :TREE
                next if t.designation.dig == :Default
                next if list.length > 4*amount and rand(4) != 0
                t.dig(:Default)
                amount -= 1
                break if amount <= 0
            }
        end

        def is_item_free(i)
            !i.flags.trader and
            !i.flags.in_job and
            !i.flags.removed and
            !i.flags.forbid and
            !i.flags.in_chest and
            (!i.flags.container or !i.itemrefs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainsItemst) }) and
            (!i.flags.in_building or !i.itemrefs.find { |ir| ir.kind_of?(DFHack::GeneralRefBuildingHolderst) and
             ir.building_tg.contained_items.find { |bi| bi.use_mode == 2 and bi.item == i } })
        end

        # return a list of trees on the map
        # lists only visible trees, sorted by distance from the fort entrance
        def tree_list
            fe = ai.plan.fort_entrance
            (df.world.plants.tree_dry.to_a + df.world.plants.tree_wet.to_a).find_all { |p|
                t = df.map_tile_at(p) and
                  t.shape == :TREE and
                  not t.designation.hidden
            }.sort_by { |p|
                (p.pos.x-fe.x)**2 + (p.pos.y-fe.y)**2 + ((p.pos.z-fe.z2)*4)**2
            }
        end

        def is_metal_ore(i)
            # mat_index => bool
            @metal_ore_cache ||= {}
            i.kind_of?(DFHack::ItemBoulderst) and i.mat_type == 0 and @metal_ore_cache.fetch(i.mat_index) {
                @metal_ore_cache[i.mat_index] = df.world.raws.inorganics[i.mat_index].flags[:METAL_ORE]
            }
        end

        def is_raw_coke(i)
            # mat_index => custom reaction name
            @raw_coke_cache ||= df.world.raws.reactions.inject({}) { |h, r|
                if r.products.length == 1 and p = r.products.first and p.kind_of?(DFHack::ReactionProductItemst) and
                        r.reagents.length == 1 and rr = r.reagents.first and rr.item_type == :BOULDER and rr.mat_type == 0 and
                        p.item_type == :BAR and mt = df.decode_mat(p) and mt.material and mt.material.id == 'COAL'
                        # XXX check input size vs output size ?
                    h.update rr.mat_index => r.code
                else
                    h
                end
            }
            i.kind_of?(DFHack::ItemBoulderst) and i.mat_type == 0 and @raw_coke_cache[i.mat_index]
        end

        def status
            @count.inspect
        end
    end
end
