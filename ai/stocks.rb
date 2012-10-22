class DwarfAI
    class Stocks
        Needed = { :bin => 6, :barrel => 6, :bucket => 4, :bag => 4,
            :food => 20, :drink => 20, :soap => 5, :logs => 10, :coal => 5,
            :pigtail_seeds => 10, :dimplecup_seeds => 10, :dimple_dye => 10,
            :splint => 2, :crutch => 2,
        }
        NeededPerDwarf = { :food => 1, :drink => 2 }

        WatchStock = { :roughgem => 6, :pigtail => 10, :cloth_nodye => 10,
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
            list = case k
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
                when :splint
                    df.world.items.other[:SPLINT] #.reject { |i| i.flags.in_inventory } # inventory set when it bin
                when :crutch
                    df.world.items.other[:CRUTCH] #.reject { |i| i.flags.in_inventory }
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
                end

            list.find_all { |i|
                !i.flags.trader and
                !i.flags.in_job and
                !i.flags.removed and
                !i.flags.forbid and
                !i.flags.in_chest and
                (!i.flags.container or !i.itemrefs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainsItemst) }) and
                (!i.flags.in_building or !i.itemrefs.find { |ir| ir.kind_of?(DFHack::GeneralRefBuildingHolderst) and
                    ir.building_tg.contained_items.find { |bi| bi.use_mode == 2 and bi.item == i } })
            }.inject(0) { |s, i| s + i.stack_size }
        end

        def queue_need(what, amount)
            case what
            when :soap, :coal
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
                amount = (amount / 5.0).ceil  # accounts for brewer yield, but not for input stack size

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
            free = df.world.items.other[:ROUGH].find_all { |i| !i.flags.trader and
                i.itemrefs.reject { |r|
                    r.kind_of?(DFHack::GeneralRefContainedInItemst)
                }.empty?
            }
            df.world.manager_orders.each { |o|
                next if o.job_type != :CutGems
                o.amount_left.times {
                    if idx = free.index { |g| g.mat_index == o.mat_index and g.mat_type == o.mat_type }
                        free.delete_at idx
                    end
                }
            }
            WatchStock[:roughgem].times { free.pop }
            return if free.empty?
            ai.plan.ensure_workshop(:Jewelers, false)
            gem = free.first
            count = free.find_all { |i| i.mat_index == gem.mat_index and i.mat_type == gem.mat_type }.length
            amount = count if amount > count

            puts "AI: stocks: queue #{amount} CutGems" if $DEBUG

            if o = df.world.manager_orders.find { |_o| _o.job_type == :CutGems and _o.mat_type == gem.mat_type and
                _o.mat_index == gem.mat_index and _o.amount_total < 30 }
                amount = [30-o.amount_total, amount].min
                o.amount_total += amount
                o.amount_left += amount
            else
                amount = 30 if amount > 30
                df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :CutGems, :unk_2 => -1, :item_subtype => -1,
                                                                        :mat_type => gem.mat_type, :mat_index => gem.mat_index,
                                                                        :amount_left => amount, :amount_total => amount)
            end
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

        def status
            @count.inspect
        end
    end
end
