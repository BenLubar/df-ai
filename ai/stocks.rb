class DwarfAI
    # an object similar to a hash, that will evaluate and cache @proc for every key
    class CacheHash
        def initialize(&b)
            @h = {}
            @proc = b
        end

        def [](i)
            @h.fetch(i) { @h[i] = @proc.call(i) }
        end
    end

    class Stocks
        Needed = {
            :door => 4, :bed => 4, :bin => 4, :barrel => 4,
            :cabinet => 4, :chest => 4, :mechanism => 4,
            :bag => 3, :table => 3, :chair => 3, :cage => 3,
            :coffin => 2, :coffin_bld => 3, :coffin_bld_pet => 1,
            :food => 20, :drink => 20, :wood => 16, :bucket => 2,
            :thread_seeds => 10, :dye_seeds => 10, :dye => 10,
            :weapon => 2, :armor => 2, :clothes => 2, :block => 6,
            :quiver => 2, :flask => 2, :backpack => 2, :wheelbarrow => 1,
            :splint => 1, :crutch => 1, :rope => 1, :weaponrack => 1,
            :armorstand => 1, :floodgate => 1, :traction_bench => 1,
            :soap => 1, :lye => 1, :ash => 1, :plasterpowder => 1,
            :coal => 3, :raw_coke => 1, :gypsum => 1,
            :giant_corkscrew => 1, :pipe_section => 1, :anvil => 1,
            :quern => 1, :minecart => 1, :nestbox => 1, :hive => 1,
            :jug => 1, :stepladder => 2, :pick => 2, :axe => 2,
        }
        NeededPerDwarf = Hash.new(0.0).update :food => 1, :drink => 2

        WatchStock = {
            :roughgem => 6, :thread_plant => 10, :cloth_nodye => 10,
            :mill_plant => 4, :bag_plant => 4, :milk => 1,
            :metal_ore => 6, :raw_coke => 2, :raw_adamantine => 2,
            :skull => 2, :bone => 8, :food_ingredients => 2,
            :drink_plant => 5, :drink_fruit => 5, :honey => 1,
            :honeycomb => 1, :wool => 1, :tallow => 1, :shell => 1,
        }

        attr_accessor :ai, :count
        attr_accessor :onupdate_handle
        def initialize(ai)
            @ai = ai
            reset
        end

        def reset
            @updating = []
            @lastupdating = 0
            @count = {}
            @farmplots = Hash.new(0)
            @seeds = Hash.new(0)
            @plants = Hash.new(0)
        end

        def startup
            update_kitchen
            update_plants
        end

        def onupdate_register
            reset
            @onupdate_handle = df.onupdate_register('df-ai stocks', 4800, 30) { update }
        end

        def onupdate_unregister
            df.onupdate_unregister(@onupdate_handle)
        end

        def status
            @count.map { |k, v| "#{k}: #{v}" }.sort.join(", ")
        end

        def update
            if not @updating.empty? and @lastupdating != @updating.length + @updating_count.length
                # avoid stall if cb_bg crashed and was unregistered
                @ai.debug 'not updating stocks'
                @lastupdating = @updating.length + @updating_count.length
                return
            end

            @last_unforbidall_year ||= df.cur_year
            if @last_unforbidall_year != df.cur_year
                @last_unforbidall_year = df.cur_year
                df.world.items.all.each { |i| i.flags.forbid = false }
            end

            # trim stalled manager orders once per month
            @last_managerstall ||= df.cur_year_tick / (1200*28)
            if @last_managerstall != df.cur_year_tick / (1200*28)
                @last_managerstall = df.cur_year_tick / (1200*28)
                if m = df.world.manager_orders.first and m.is_validated
                    if m.job_type == @last_managerorder 
                        if m.amount_left > 3
                            m.amount_left -= 3
                        else
                            df.world.manager_orders.delete_at(0)
                            #m._cpp_delete # TODO once dfhack-0.34.11-r4 is out
                        end
                    else
                        @last_managerorder = m.job_type
                    end
                end
            end

            @updating = Needed.keys | WatchStock.keys
            @updating_count = @updating.dup
            @updating_seeds = true
            @updating_plants = true
            @updating_farmplots = []

            ai.plan.find_room(:farmplot) { |r|
                @updating_farmplots << r if r.dfbuilding
                false # search all farm plots
            }

            @ai.debug 'updating stocks'

            # do stocks accounting 'in the background' (ie one bit at a time)
            cb_bg = df.onupdate_register('df-ai stocks bg', 8) {
                if @updating_seeds
                    cb_bg.description = 'df-ai stocks bg count_seeds'
                    count_seeds
                elsif @updating_plants
                    cb_bg.description = 'df-ai stocks bg count_plants'
                    count_plants
                elsif key = @updating_count.shift
                    cb_bg.description = "df-ai stocks bg count #{key}"
                    @count[key] = count_stocks(key)
                elsif key = @updating.shift
                    cb_bg.description = "df-ai stocks bg act #{key}"
                    act(key)
                elsif r = @updating_farmplots.shift
                    cb_bg.description = "df-ai stocks bg farmplot #{r.subtype} #{ai.plan.rooms.index(r)}"
                    farmplot(r, false)
                else
                    # finished, dismiss callback
                    df.onupdate_unregister(cb_bg)
                end
            }
        end

        def update_kitchen
            # ban cooking alcohol
            df.world.raws.plants.all.each do |p|
                p.material.each do |m|
                    ban_cooking("PLANT:#{p.id}:#{m.id}", :DRINK) if m.flags[:ALCOHOL]
                end
            end
            df.world.raws.creatures.all.each do |c|
                c.material.each do |m|
                    ban_cooking("CREATURE:#{c.creature_id}:#{m.id}", :DRINK) if m.flags[:ALCOHOL]
                end
            end
            # ban cooking tallow
            df.world.raws.creatures.all.each do |c|
                c.material.each do |m|
                    ban_cooking("CREATURE:#{c.creature_id}:#{m.id}", :GLOB) if m.reaction_product and m.reaction_product.id and m.reaction_product.id[0] == 'SOAP_MAT'
                end
            end
            # ban cooking honey (hard-coded in raws)
            ban_cooking('CREATURE:HONEY_BEE:HONEY', :LIQUID_MISC)
            # ban cooking plants that have seeds
            df.world.raws.plants.all.each do |p|
                m = df.decode_mat(p.material_defs.type_basic_mat, p.material_defs.idx_basic_mat).material
                ban_cooking("PLANT:#{p.id}:#{p.material_defs.str_basic_mat[1]}", :PLANT) if m.reaction_product and m.reaction_product.id and m.reaction_product.id.include?('SEED_MAT')

                if not p.flags[:TREE]
                    p.growths.each do |g|
                        m = df.decode_mat(g).material
                        ban_cooking("PLANT:#{p.id}:#{m.id}", :PLANT_GROWTH) if m.reaction_product and m.reaction_product.id and m.reaction_product.id.include?('SEED_MAT')
                    end
                end
            end
        end

        def update_plants
            DrinkPlants.clear
            DrinkFruits.clear
            ThreadPlants.clear
            MillPlants.clear
            BagPlants.clear
            DyePlants.clear
            GrowPlants.clear
            MilkCreatures.clear
            df.world.raws.plants.all.length.times do |i|
                p = df.world.raws.plants.all[i]
                p.material.length.times do |j|
                    if p.material[j].reaction_product.id.include?('DRINK_MAT')
                        if p.material[j].flags[:STRUCTURAL_PLANT_MAT]
                            DrinkPlants[i] = j + 419
                        elsif p.material[j].flags[:LEAF_MAT]
                            DrinkFruits[i] = j + 419
                        end
                        break
                    end
                end
                ThreadPlants[i] = p.material_defs.type_basic_mat if p.flags[:THREAD]
                MillPlants[i]   = p.material_defs.type_basic_mat if p.flags[:MILL]
                BagPlants[i]    = p.material_defs.type_basic_mat if df.decode_mat(p.material_defs.type_basic_mat, p.material_defs.idx_basic_mat).material.reaction_product.id.include?('BAG_ITEM')
                DyePlants[i]    = p.material_defs.type_mill      if p.flags[:MILL] and df.decode_mat(p.material_defs.type_mill, p.material_defs.idx_mill).material.flags[:IS_DYE]
                GrowPlants[i]   = p.material_defs.type_basic_mat if p.flags[:SEED] and p.flags[:BIOME_SUBTERRANEAN_WATER]
            end
            df.world.raws.creatures.all.length.times do |i|
                c = df.world.raws.creatures.all[i]
                c.material.length.times do |j|
                    if c.material[j].reaction_product.id.include?('CHEESE_MAT')
                        MilkCreatures[i] = j + 19
                        break
                    end
                end
            end
        end

        def count_seeds
            @farmplots = Hash.new(0)
            ai.plan.find_room(:farmplot) { |r|
                next unless bld = r.dfbuilding

                4.times { |season|
                    @farmplots[[season, bld.plant_id[season]]] += 1
                }

                false # search all farm plots
            }
            @seeds = Hash.new(0)
            df.world.items.other[:SEEDS].each { |i|
                @seeds[i.mat_index] += i.stack_size if is_item_free(i)
            }
            @updating_seeds = false
        end

        def count_plants
            @plants = Hash.new(0)
            df.world.items.other[:PLANT].each { |i|
                @plants[i.mat_index] += i.stack_size if is_item_free(i)
            }
            df.world.items.other[:PLANT_GROWTH].each { |i|
                @plants[i.mat_index] += i.stack_size if is_item_free(i)
            }
            @updating_plants = false
        end

        def ban_cooking(material, type, subtype=-1)
            @ai.debug "ban_cooking #{material}"
            material = df.decode_mat(material)
            df.ui.kitchen.item_types.length.times do |i|
                next if df.ui.kitchen.item_types[i]    != type
                next if df.ui.kitchen.item_subtypes[i] != subtype
                next if df.ui.kitchen.mat_types[i]     != material.mat_type
                next if df.ui.kitchen.mat_indices[i]   != material.mat_index
                return
            end
            df.ui.kitchen.item_types    << type
            df.ui.kitchen.item_subtypes << subtype
            df.ui.kitchen.mat_types     << material.mat_type
            df.ui.kitchen.mat_indices   << material.mat_index
            df.ui.kitchen.exc_types     << 1
        end

        def act(key)
            if amount = Needed[key]
                amount += (@ai.pop.citizen.length * NeededPerDwarf[key]).to_i
                queue_need(key, amount*3/2-@count[key]) if @count[key] < amount
            end
            
            if amount = WatchStock[key]
                queue_use(key, @count[key]-amount) if @count[key] > amount
            end
        end

        DrinkPlants   = {} # plants that can be brewed in their basic form
        DrinkFruits   = {} # plants that have a growth that can be brewed
        ThreadPlants  = {} # plants that can be made into thread
        MillPlants    = {} # plants that can be milled
        BagPlants     = {} # plants that can be processed into a bag
        DyePlants     = {} # plants that can be milled that are able to dye cloth
        GrowPlants    = {} # plants that we can grow underground
        MilkCreatures = {} # creatures that have milk that can be turned into cheese

        # count unused stocks of one type of item
        def count_stocks(k)
            case k
            when :bin
                df.world.items.other[:BIN].find_all { |i| i.stockpile.id == -1 }
            when :barrel
                df.world.items.other[:BARREL].find_all { |i| i.stockpile.id == -1 }
            when :bag
                df.world.items.other[:BOX].find_all { |i| df.decode_mat(i).plant }
            when :rope
                df.world.items.other[:CHAIN].find_all { |i| df.decode_mat(i).plant }
            when :bucket
                df.world.items.other[:BUCKET]
            when :food
                df.world.items.other[:ANY_GOOD_FOOD].grep(DFHack::ItemFoodst)
            when :food_ingredients
                forbidden = {}
                df.ui.kitchen.item_types.to_a.zip(df.ui.kitchen.item_subtypes.to_a, df.ui.kitchen.mat_types.to_a, df.ui.kitchen.mat_indices.to_a).each do |x|
                    forbidden[x] = true
                end

                df.world.items.other[:ANY_COOKABLE].reject do |item|
                    item.kind_of?(DFHack::ItemFlaskst) or
                    item.kind_of?(DFHack::ItemCagest) or
                    item.kind_of?(DFHack::ItemBarrelst) or
                    item.kind_of?(DFHack::ItemBucketst) or
                    item.kind_of?(DFHack::ItemAnimaltrapst) or
                    item.kind_of?(DFHack::ItemBoxst) or
                    item.kind_of?(DFHack::ItemFishRawst) or
                    item.kind_of?(DFHack::ItemToolst) or
                    forbidden[[item.getType, item.getSubtype, item.getMaterial, item.getMaterialIndex]]
                end
            when :drink
                df.world.items.other[:DRINK]
            when :soap, :coal, :ash
                mat_id = {:soap => 'SOAP', :coal => 'COAL', :ash => 'ASH'}[k]
                df.world.items.other[:BAR].find_all { |i|
                    mat = df.decode_mat(i) and mat.material and mat.material.id == mat_id
                }
            when :wood
                df.world.items.other[:WOOD]
            when :roughgem
                df.world.items.other[:ROUGH].find_all { |i| i.mat_type == 0 }
            when :metal_ore
                df.world.items.other[:BOULDER].find_all { |i| is_metal_ore(i) }
            when :raw_coke
                df.world.items.other[:BOULDER].find_all { |i| is_raw_coke(i) }
            when :gypsum
                df.world.items.other[:BOULDER].find_all { |i| is_gypsum(i) }
            when :raw_adamantine
                df.world.items.other[:BOULDER].grep(df.decode_mat('INORGANIC:RAW_ADAMANTINE'))
            when :splint
                df.world.items.other[:SPLINT]
            when :crutch
                df.world.items.other[:CRUTCH]
            when :crossbow
                df.world.items.other[:WEAPON].find_all { |i|
                    i.subtype.subtype == ManagerSubtype[:MakeBoneCrossbow]
                }
            when :drink_plant, :thread_plant, :mill_plant, :bag_plant
                plant = {
                    :drink_plant  => DrinkPlants,
                    :thread_plant => ThreadPlants,
                    :mill_plant   => MillPlants,
                    :bag_plant    => BagPlants,
                }[k]
                df.world.items.other[:PLANT].find_all do |i|
                    plant[i.mat_index] == i.mat_type
                end
            when :drink_fruit
                df.world.items.other[:PLANT_GROWTH].find_all do |i|
                    DrinkFruits[i.mat_index] == i.mat_type
                end
            when :honey
                df.world.items.other[:LIQUID_MISC].grep(df.decode_mat('CREATURE:HONEY_BEE:HONEY'))
            when :milk
                df.world.items.other[:LIQUID_MISC].find_all do |i|
                    MilkCreatures[i.mat_index] == i.mat_type
                end
            when :dye_plant
                df.world.items.other[:PLANT].find_all do |i|
                    MillPlants[i.mat_index] == i.mat_type and DyePlants[i.mat_index]
                end
            when :thread_seeds, :dye_seeds
                plant = {
                    :thread_seeds => ThreadPlants,
                    :dye_seeds    => DyePlants,
                }[k]
                df.world.items.other[:SEEDS].find_all do |i|
                    plant[i.mat_index] and GrowPlants[i.mat_index]
                end
            when :dye
                df.world.items.other[:POWDER_MISC].find_all do |i|
                    DyePlants[i.mat_index] == i.mat_type
                end
            when :block
                df.world.items.other[:BLOCKS]
            when :skull
                # XXX exclude dwarf skulls ?
                df.world.items.other[:CORPSEPIECE].find_all { |i|
                    i.corpse_flags.skull and not i.corpse_flags.unbutchered
                }
            when :bone
                return df.world.items.other[:CORPSEPIECE].find_all { |i|
                    i.corpse_flags.bone and not i.corpse_flags.unbutchered
                }.inject(0) { |s, i|
                    # corpsepieces uses this instead of i.stack_size
                    s + i.material_amount[:Bone]
                }
            when :shell
                return df.world.items.other[:CORPSEPIECE].find_all { |i|
                    i.corpse_flags.shell and not i.corpse_flags.unbutchered
                }.inject(0) { |s, i|
                    # corpsepieces uses this instead of i.stack_size
                    s + i.material_amount[:Shell]
                }
            when :wool
                df.world.items.other[:CORPSEPIECE].find_all { |i|
                    i.corpse_flags.hair_wool or i.corpse_flags.yarn
                #}.inject(0) { |s, i|
                    # used for SpinThread which currently ignores the material_amount
                    # note: if it didn't, use either HairWool or Yarn but not both
                }
            when :bonebolts
                df.world.items.other[:AMMO].find_all { |i|
                    i.skill_used == :BONECARVE
                }
            when :cloth
                df.world.items.other[:CLOTH]
            when :cloth_nodye
                df.world.items.other[:CLOTH].find_all { |i|
                    !i.improvements.find { |imp| imp.dye.mat_type != -1 }
                }
            when :mechanism
                df.world.items.other[:TRAPPARTS]
            when :cage
                df.world.items.other[:CAGE].reject { |i|
                    i.general_refs.grep(DFHack::GeneralRefContainsUnitst).first or
                    i.general_refs.grep(DFHack::GeneralRefContainsItemst).first or
                    (ref = i.general_refs.grep(DFHack::GeneralRefBuildingHolderst).first and
                     ref.building_tg.kind_of?(DFHack::BuildingTrapst))
                }
            when :coffin_bld
                # count free constructed coffin buildings, not items
                return df.world.buildings.other[:COFFIN].find_all { |bld| !bld.owner }.length
            when :coffin_bld_pet
                return df.world.buildings.other[:COFFIN].find_all { |bld| !bld.owner and !bld.burial_mode.no_pets }.length
            when :weapon
                return count_stocks_weapon
            when :pick
                return count_stocks_weapon(:MINING)
            when :axe
                return count_stocks_weapon(:AXE)
            when :armor
                return count_stocks_armor
            when :clothes
                return count_stocks_clothes
            when :lye
                df.world.items.other[:LIQUID_MISC].find_all { |i|
                    mat = df.decode_mat(i) and mat.material and mat.material.id == 'LYE'
                    # TODO check container has no water
                }
            when :plasterpowder
                df.world.items.other[:POWDER_MISC].find_all { |i|
                    mat = df.decode_mat(i) and mat.inorganic and mat.inorganic.id == 'PLASTER'
                }
            when :wheelbarrow, :minecart, :nestbox, :hive, :jug, :stepladder
                ord = FurnitureOrder[k]
                df.world.items.other[:TOOL].find_all { |i|
                    i.subtype.subtype == ManagerSubtype[ord] and
                    i.stockpile.id == -1 and
                    (!i.vehicle_tg or i.vehicle_tg.route_id == -1)
                }
            when :honeycomb
                df.world.items.other[:TOOL].find_all { |i| i.subtype.id == 'ITEM_TOOL_HONEYCOMB' }
            when :quiver
                df.world.items.other[:QUIVER]
            when :flask
                df.world.items.other[:FLASK]
            when :backpack
                df.world.items.other[:BACKPACK]
            when :leather
                df.world.items.other[:SKIN_TANNED]
            when :tallow
                df.world.items.other[:GLOB].find_all { |i|
                    mat = df.decode_mat(i) and mat.material and mat.material.id == 'TALLOW'
                }
            when :giant_corkscrew
                df.world.items.other[:TRAPCOMP].find_all { |i|
                    i.subtype.subtype == ManagerSubtype[:MakeGiantCorkscrew]
                }
            when :pipe_section
                df.world.items.other[:PIPE_SECTION]
            when :quern
                # include used in building
                return df.world.items.other[:QUERN].length
            when :anvil
                df.world.items.other[:ANVIL]
            else
                return find_furniture_itemcount(k)

            end.find_all { |i|
                is_item_free(i)
            }.inject(0) { |s, i| s + i.stack_size }
        end

        # return the minimum of the number of free weapons for each subtype used by current civ
        def count_stocks_weapon(skill=nil)
            ue = df.ui.main.fortress_entity.entity_raw.equipment
            [[:WEAPON, ue.digger_tg],
             [:WEAPON, ue.weapon_tg]].map { |oidx, idefs|
                idefs.to_a.map { |idef|
                    next if skill and idef.skill_melee != skill
                    next if idef.flags[:TRAINING]
                    df.world.items.other[oidx].find_all { |i|
                        i.subtype.subtype == idef.subtype and
                        i.mat_type == 0 and
                        is_item_free(i)
                    }.length
                }.compact
            }.flatten.min
        end

        # return the minimum count of free metal armor piece per subtype
        def count_stocks_armor
            ue = df.ui.main.fortress_entity.entity_raw.equipment
            [[:ARMOR, ue.armor_tg],
             [:SHIELD, ue.shield_tg],
             [:HELM, ue.helm_tg],
             [:PANTS, ue.pants_tg],
             [:GLOVES, ue.gloves_tg],
             [:SHOES, ue.shoes_tg]].map { |oidx, idefs|
                div = 1
                div = 2 if oidx == :GLOVES or oidx == :SHOES
                idefs.to_a.map { |idef|
                    next unless idef.kind_of?(DFHack::ItemdefShieldst) or idef.props.flags[:METAL]
                    df.world.items.other[oidx].find_all { |i|
                        i.subtype.subtype == idef.subtype and
                        i.mat_type == 0 and
                        is_item_free(i)
                    }.length / div
                }.compact
            }.flatten.min
        end

        def count_stocks_clothes
            ue = df.ui.main.fortress_entity.entity_raw.equipment
            [[:ARMOR, ue.armor_tg],
             [:HELM, ue.helm_tg],
             [:PANTS, ue.pants_tg],
             [:GLOVES, ue.gloves_tg],
             [:SHOES, ue.shoes_tg]].map { |oidx, idefs|
                div = 1
                div = 2 if oidx == :GLOVES or oidx == :SHOES
                idefs.to_a.map { |idef|
                    next unless idef.props.flags[:SOFT] # XXX
                    df.world.items.other[oidx].find_all { |i|
                        i.subtype.subtype == idef.subtype and
                        i.mat_type != 0 and # XXX
                        i.wear == 0 and
                        is_item_free(i)
                    }.length / div
                }.compact
            }.flatten.min
        end


        # make it so the stocks of 'what' rises by 'amount'
        def queue_need(what, amount)
            return if amount <= 0

            case what
            when :weapon
                return queue_need_weapon(Needed[:weapon])
            when :pick
                return queue_need_weapon(Needed[:pick], :MINING)
            when :axe
                return queue_need_weapon(Needed[:axe], :AXE)
            when :armor
                return queue_need_armor
            when :anvil
                return queue_need_anvil
            when :clothes
                return queue_need_clothes
            when :coffin_bld
                return queue_need_coffin_bld(amount)
            when :coffin_bld_pet
                if count[:coffin_bld] >= Needed[:coffin_bld] and
                        cof = df.world.buildings.other[:COFFIN].find { |bld| !bld.owner and bld.burial_mode.no_pets }
                    cof.burial_mode.no_pets = false
                end
                return
            when :raw_coke
                if @ai.plan.past_initial_phase and raw = @ai.plan.map_veins.keys.find { |k| is_raw_coke(k) }
                    @ai.plan.dig_vein(raw)
                end
                return
            when :gypsum
                if @ai.plan.past_initial_phase and raw = @ai.plan.map_veins.keys.find { |k| is_gypsum(k) }
                    @ai.plan.dig_vein(raw)
                end
                return
            when :food
                # XXX fish/hunt/cook ?
                @last_warn_food ||= Time.now-610    # warn every 10mn
                if @last_warn_food < Time.now-600
                    puts "AI: need #{amount} more food"
                    @last_warn_food = Time.now
                end
                return

            when :thread_seeds, :dye_seeds
                # only useful at game start, with low seeds stocks
                input = {
                    :thread_seeds => [:thread_plant],
                    :dye_seeds    => [:dye_plant, :bag],
                }[what]

                order = {
                    :thread_seeds => :ProcessPlants,
                    :dye_seeds    => :MillPlants,
                }[what]

            when :dye
                order = :MillPlants
                input = [:dye_plants, :bag]

            when :wood
                # dont bother if the last designated tree is not cut yet
                return if cutting_trees?

                amount *= 2
                amount = 30 if amount > 30
                tl = tree_list
                tl.each { |t|
                    amount -= 6 if tile = df.map_tile_at(*t) and tile.designation and tile.designation.dig == :Default
                }
                @last_cutpos = cuttrees(amount/6, tl) if amount > 6
                return

            when :honey
                order = :PressHoneycomb
                input = [:honeycomb, :jug]

            when :drink
                orders = {
                    :drink_plant => :BrewDrinkPlant,
                    :drink_fruit => :BrewDrinkFruit,
                    :honey       => :BrewMead,
                }
                input = [[:drink_plant, :drink_fruit, :honey].max_by{ |i|
                    c = count[i] || count_stocks(i)
                    find_manager_orders(orders[i]).each { |o| c -= o.amount_left }
                    c
                }, :barrel]
                order = orders[input.first]
                amount = (amount+4)/5  # accounts for brewer yield, but not for input stack size

            when :block
                amount = (amount+3)/4
                # no stone => make wooden blocks (needed for pumps for aquifer handling)
                if not df.world.items.other[:BOULDER].find { |i|
                        is_item_free(i) and !df.ui.economic_stone[i.mat_index]
                        # TODO check the boulders we find there are reachable..
                    }
                    amount = 2 if amount > 2
                    order = :ConstructWoodenBlocks
                end

            when :coal
                # dont use wood -> charcoal if we have bituminous coal
                # (except for bootstraping)
                amount = 2-@count[:coal] if amount > 2-@count[:coal] and @count[:raw_coke] > WatchStock[:raw_coke]

            when :ash
                input = [:wood]
            when :lye
                input = [:ash, :bucket]
            when :soap
                input = [:lye, :tallow]
            when :plasterpowder
                input = [:gypsum, :bag]
            end

            order ||= FurnitureOrder[what]
            @ai.debug "stocks: need #{amount} #{order} for #{what}"

            if input
                i_amount = amount
                input.each { |i|
                    c = count[i] || count_stocks(i)
                    i_amount = c if c < i_amount
                    if c < amount and Needed[i]
                        @ai.debug "stocks: want #{amount - c} more #{i} for #{i_amount}/#{amount} #{order}"
                        queue_need(i, amount - c)
                    end
                }
                amount = i_amount
            end

            if matcat = ManagerMatCategory[order]
                i_amount = (count[matcat] || count_stocks(matcat)) - count_manager_orders_matcat(matcat, order)
                if i_amount < amount and Needed[matcat]
                    @ai.debug "stocks: want #{amount - i_amount} more #{matcat} for #{i_amount}/#{amount} #{order}"
                    queue_need(matcat, amount - i_amount)
                end
                amount = i_amount if amount > i_amount
            end

            amount = 30 if amount > 30

            find_manager_orders(order).each { |o| amount -= o.amount_total }

            return if amount <= 0

            add_manager_order(order, amount)
        end

        # forge weapons
        def queue_need_weapon(needed, skill=nil)
            return if not skill and (@count[:pick] == 0 or @count[:axe] == 0)

            bars = Hash.new(0)
            coal_bars = @count[:coal]
            coal_bars = 50000 if !df.world.buildings.other[:FURNACE_SMELTER_MAGMA].empty?

            df.world.items.other[:BAR].each { |i|
                bars[i.mat_index] += i.stack_size if i.mat_type == 0
            }

            # rough account of already queued jobs consumption
            df.world.manager_orders.each { |mo|
                if mo.mat_type == 0
                    bars[mo.mat_index] -= 4*mo.amount_total
                    coal_bars -= mo.amount_total
                end
            }

            @metal_digger_pref ||= (0...df.world.raws.inorganics.length).find_all { |mi|
                df.world.raws.inorganics[mi].material.flags[:ITEMS_DIGGER]
            }.sort_by { |mi|    # should roughly order metals by effectiveness
                - df.world.raws.inorganics[mi].material.strength.yield[:IMPACT]
            }

            @metal_weapon_pref ||= (0...df.world.raws.inorganics.length).find_all { |mi|
                df.world.raws.inorganics[mi].material.flags[:ITEMS_WEAPON]
            }.sort_by { |mi|
                - df.world.raws.inorganics[mi].material.strength.yield[:IMPACT]
            }

            may_forge_cache = CacheHash.new { |mi| may_forge_bars(mi) }

            ue = df.ui.main.fortress_entity.entity_raw.equipment
            [[ue.digger_tg, @metal_digger_pref],
             [ue.weapon_tg, @metal_weapon_pref]].each { |idefs, pref|
                idefs.each { |idef|
                    next if skill and idef.skill_melee != skill
                    next if idef.flags[:TRAINING]

                    cnt = needed
                    cnt -= df.world.items.other[:WEAPON].find_all { |i|
                        i.subtype.subtype == idef.subtype and is_item_free(i)
                    }.length
                    df.world.manager_orders.each { |mo|
                        cnt -= mo.amount_total if mo.job_type == :MakeWeapon and mo.item_subtype == idef.subtype
                    }
                    next if cnt <= 0

                    need_bars = idef.material_size / 3  # need this many bars to forge one idef item
                    need_bars = 1 if need_bars < 1

                    pref.each { |mi|
                        break if bars[mi] < need_bars and may_forge_cache[mi]
                        nw = bars[mi] / need_bars
                        nw = coal_bars if nw > coal_bars
                        nw = cnt if nw > cnt
                        next if nw <= 0

                        @ai.debug "stocks: queue #{nw} MakeWeapon #{df.world.raws.inorganics[mi].id} #{idef.id}"
                        df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :MakeWeapon, :unk_2 => -1,
                                :item_subtype => idef.subtype, :mat_type => 0, :mat_index => mi, :amount_left => nw, :amount_total => nw)
                        bars[mi] -= nw * need_bars
                        coal_bars -= nw
                        cnt -= nw
                        break if may_forge_cache[mi]    # dont use lesser metal
                    }
                }
            }
        end

        # forge armor pieces
        def queue_need_armor
            bars = Hash.new(0)
            coal_bars = @count[:coal]
            coal_bars = 50000 if !df.world.buildings.other[:FURNACE_SMELTER_MAGMA].empty?

            df.world.items.other[:BAR].each { |i|
                bars[i.mat_index] += i.stack_size if i.mat_type == 0
            }

            # rough account of already queued jobs consumption
            df.world.manager_orders.each { |mo|
                if mo.mat_type == 0 and bars.has_key?(mo.mat_index)
                    bars[mo.mat_index] -= 4*mo.amount_total
                    coal_bars -= mo.amount_total
                end
            }

            @metal_armor_pref ||= (0...df.world.raws.inorganics.length).find_all { |mi|
                df.world.raws.inorganics[mi].material.flags[:ITEMS_ARMOR]
            }.sort_by { |mi|
                - df.world.raws.inorganics[mi].material.strength.yield[:IMPACT]
            }

            may_forge_cache = CacheHash.new { |mi| may_forge_bars(mi) }

            ue = df.ui.main.fortress_entity.entity_raw.equipment
            [[:ARMOR, ue.armor_tg],
             [:SHIELD, ue.shield_tg],
             [:HELM, ue.helm_tg],
             [:PANTS, ue.pants_tg],
             [:GLOVES, ue.gloves_tg],
             [:SHOES, ue.shoes_tg]].each { |oidx, idefs|
                div = 1
                div = 2 if oidx == :GLOVES or oidx == :SHOES
                idefs.each { |idef|
                    next unless idef.kind_of?(DFHack::ItemdefShieldst) or idef.props.flags[:METAL]

                    job = DFHack::JobType::Item.index(oidx)     # :GLOVES => :MakeGloves

                    cnt = Needed[:armor]
                    cnt -= df.world.items.other[oidx].find_all { |i|
                        i.subtype.subtype == idef.subtype and i.mat_type == 0 and is_item_free(i)
                    }.length / div

                    df.world.manager_orders.each { |mo|
                        cnt -= mo.amount_total if mo.job_type == job and mo.item_subtype == idef.subtype
                    }
                    next if cnt <= 0

                    need_bars = idef.material_size / 3  # need this many bars to forge one idef item
                    need_bars = 1 if need_bars < 1

                    @metal_armor_pref.each { |mi|
                        break if bars[mi] < need_bars and may_forge_cache[mi]
                        nw = bars[mi] / need_bars
                        nw = coal_bars if nw > coal_bars
                        nw = cnt if nw > cnt
                        next if nw <= 0

                        @ai.debug "stocks: queue #{nw} #{job} #{df.world.raws.inorganics[mi].id} #{idef.id}"
                        df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => job, :unk_2 => -1,
                                :item_subtype => idef.subtype, :mat_type => 0, :mat_index => mi, :amount_left => nw, :amount_total => nw)
                        bars[mi] -= nw * need_bars
                        coal_bars -= nw
                        cnt -= nw
                        break if may_forge_cache[mi]
                    }
                }
            }
        end

        def queue_need_anvil
            bars = Hash.new(0)
            coal_bars = @count[:coal]
            coal_bars = 50000 if !df.world.buildings.other[:FURNACE_SMELTER_MAGMA].empty?

            df.world.items.other[:BAR].each { |i|
                bars[i.mat_index] += i.stack_size if i.mat_type == 0
            }

            # rough account of already queued jobs consumption
            df.world.manager_orders.each { |mo|
                if mo.mat_type == 0 and bars.has_key?(mo.mat_index)
                    bars[mo.mat_index] -= 4*mo.amount_total
                    coal_bars -= mo.amount_total
                end
            }

            @metal_anvil_pref ||= (0...df.world.raws.inorganics.length).find_all { |mi|
                df.world.raws.inorganics[mi].material.flags[:ITEMS_ANVIL]
            }

            may_forge_cache = CacheHash.new { |mi| may_forge_bars(mi) }

            cnt = Needed[:anvil]
            cnt -= @count[:anvil]

            df.world.manager_orders.each { |mo|
                cnt -= mo.amount_total if mo.job_type == :ForgeAnvil
            }
            return if cnt <= 0

            need_bars = 1

            @metal_anvil_pref.each { |mi|
                break if bars[mi] < need_bars and may_forge_cache[mi]
                nw = bars[mi] / need_bars
                nw = coal_bars if nw > coal_bars
                nw = cnt if nw > cnt
                next if nw <= 0

                @ai.debug "stocks: queue #{nw} ForgeAnvil #{df.world.raws.inorganics[mi].id}"
                df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :ForgeAnvil, :unk_2 => -1,
                    :item_subtype => -1, :mat_type => 0, :mat_index => mi, :amount_left => nw, :amount_total => nw)
                bars[mi] -= nw * need_bars
                coal_bars -= nw
                cnt -= nw
                break if may_forge_cache[mi]
            }
        end

        def queue_need_clothes
            # -10 to try to avoid cancel spam
            available_cloth = count_stocks(:cloth) - 20

            ue = df.ui.main.fortress_entity.entity_raw.equipment
            [[:ARMOR, ue.armor_tg],
             [:HELM, ue.helm_tg],
             [:PANTS, ue.pants_tg],
             [:GLOVES, ue.gloves_tg],
             [:SHOES, ue.shoes_tg]].map { |oidx, idefs|
                div = 1
                div = 2 if oidx == :GLOVES or oidx == :SHOES
                idefs.to_a.map { |idef|
                    next unless idef.props.flags[:SOFT] # XXX

                    job = DFHack::JobType::Item.index(oidx)

                    cnt = Needed[:clothes]
                    cnt -= df.world.items.other[oidx].find_all { |i|
                        i.subtype.subtype == idef.subtype and
                        i.mat_type != 0 and
                        i.wear == 0 and
                        is_item_free(i)
                    }.length / div

                    df.world.manager_orders.each { |mo|
                        cnt -= mo.amount_total if mo.job_type == job and mo.item_subtype == idef.subtype
                        # TODO subtract available_cloth too
                    }
                    cnt = available_cloth if cnt > available_cloth
                    next if cnt <= 0

                    @ai.debug "stocks: queue #{cnt} #{job} cloth #{idef.id}"
                    df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => job, :unk_2 => -1,
                            :item_subtype => idef.subtype, :mat_type => -1, :mat_index => -1,
                            :material_category => { :cloth => true },
                            :amount_left => cnt, :amount_total => cnt)

                    available_cloth -= cnt
                }
            }
        end

        def queue_need_coffin_bld(amount)
            # dont dig too early
            return if not @ai.plan.find_room(:cemetary) { |r| r.status != :plan }


            # count actually allocated (plan wise) coffin buildings
            return if @ai.plan.find_room(:cemetary) { |r|
                r.layout.each { |f|
                    amount -= 1 if f[:item] == :coffin and not f[:bld_id] and not f[:ignore]
                }
                amount <= 0
            }

            amount.times { ai.plan.getcoffin }
        end
        

        # make it so the stocks of 'what' decrease by 'amount'
        def queue_use(what, amount)
            return if amount <= 0

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

            when :raw_adamantine
                order = :ExtractMetalStrands

            when :drink_plant, :drink_fruit, :thread_plant, :mill_plant, :bag_plant
                order = {
                    :drink_plant  => :BrewDrinkPlant,
                    :drink_fruit  => :BrewDrinkFruit,
                    :thread_plant => :ProcessPlants,
                    :mill_plant   => :MillPlants,
                    :bag_plant    => :ProcessPlantsBag,
                }[what]
                # stuff may rot/be brewed before we can process it
                amount /= 2 if amount > 10
                amount /= 2 if amount > 4
                input = [:barrel] if order == :BrewDrinkPlant or order == :BrewDrinkFruit
                input = [:bag] if order == :MillPlants or order == :ProcessPlantsBag

            when :food_ingredients
                order = :PrepareMeal
                amount = (amount + 4) / 5

            when :skull
                order = :MakeTotem

            when :bone
                nhunters = @ai.pop.labor_worker[:HUNT].length if @ai.pop.labor_worker
                return if not nhunters
                need_crossbow = nhunters + 1 - count_stocks(:crossbow)
                if need_crossbow > 0
                    order = :MakeBoneCrossbow
                    amount = need_crossbow if amount > need_crossbow
                else
                    order = :MakeBoneBolt
                    stock = count_stocks(:bonebolts)
                    amount = 1000 - stock if amount > 1000 - stock
                    amount /= 2 if amount > 10
                    amount /= 2 if amount > 4
                end

            when :shell
                order = :DecorateWithShell

            when :wool
                order = :SpinThread

            when :cloth_nodye
                order = :DyeCloth
                input = [:dye]
                amount /= 2 if amount > 10
                amount /= 2 if amount > 4

            when :honeycomb
                order = :PressHoneycomb
                input = [:jug]

            when :honey
                order = :BrewMead
                input = [:barrel]

            when :milk
                order = :MakeCheese

            when :tallow
                order = :MakeSoap
                input = [:lye]
            end

            @ai.debug "stocks: use #{amount} #{order} for #{what}"

            if input
                i_amount = amount
                input.each { |i|
                    c = count[i] || count_stocks(i)
                    i_amount = c if c < i_amount
                    if c < amount and Needed[i]
                        @ai.debug "stocks: want #{amount - c} more #{i} for #{i_amount}/#{amount} #{order}"
                        queue_need(i, amount - c)
                    end
                }
                amount = i_amount
            end

            amount = 30 if amount > 30

            find_manager_orders(order).each { |o| amount -= o.amount_total }

            return if amount <= 0

            add_manager_order(order, amount)
        end


        # cut gems
        def queue_use_gems(amount)
            return if df.world.manager_orders.find { |mo| mo.job_type == :CutGems }
            return if not i = df.world.items.other[:ROUGH].find { |_i| _i.mat_type == 0 and is_item_free(_i) }
            this_amount = df.world.items.other[:ROUGH].find_all { |_i|
                _i.mat_type == i.mat_type and _i.mat_index == i.mat_index and is_item_free(_i)
            }.length
            amount = this_amount if this_amount < amount
            amount = amount*3/4 if amount >= 10
            amount = 30 if amount > 30

            @ai.debug "stocks: queue #{amount} CutGems #{df.decode_mat(i)}"
            df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :CutGems, :unk_2 => -1, :item_subtype => -1,
                    :mat_type => i.mat_type, :mat_index => i.mat_index, :amount_left => amount, :amount_total => amount)
        end


        # smelt metal ores
        def queue_use_metal_ore(amount)
            # make coke from bituminous coal has priority
            return if @count[:raw_coke] > WatchStock[:raw_coke] and @count[:coal] < 100

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

            @ai.debug "stocks: queue #{amount} SmeltOre #{df.decode_mat(i)}"
            df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :SmeltOre, :unk_2 => -1, :item_subtype => -1,
                    :mat_type => i.mat_type, :mat_index => i.mat_index, :amount_left => amount, :amount_total => amount)
        end


        # bituminous_coal -> coke
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

            @ai.debug "stocks: queue #{amount} #{reaction}"
            df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :CustomReaction, :unk_2 => -1, :item_subtype => -1,
                    :mat_type => -1, :mat_index => -1, :amount_left => amount, :amount_total => amount, :reaction_name => reaction)
        end



        # designate some trees for woodcutting
        def cuttrees(amount, list=tree_list)
            # return the bottom-rightest designated tree
            br = nil
            list.each { |tree|
                t = df.map_tile_at(tree[0], tree[1], tree[2])
                next if t.tilemat != :TREE
                next if t.designation.dig == :Default
                next if list.length > 4*amount and rand(4) != 0
                t.dig(:Default)
                br = [t.x, t.y, t.z] if not br or (br[0] & -16) < (t.x & -16) or
                        ((br[0] & -16) == (t.x & -16) and (br[1] & -16) < (t.y & -16))
                amount -= 1
                break if amount <= 0
            }
            br
        end

        # return a list of trees on the map
        # lists only visible trees, sorted by distance from the fort entrance
        # expensive method, dont call often
        def tree_list
            fe = @ai.plan.fort_entrance
            # avoid re-scanning full map if there was no visible tree last time
            return [] if @last_treelist and @last_treelist.empty? and rand(6) > 0

            @last_treelist = (df.world.plants.tree_dry.to_a + df.world.plants.tree_wet.to_a).find_all { |p|
                t = df.map_tile_at(p) and
                t.tilemat == :TREE and
                not t.designation.hidden
            }.sort_by { |p|
                (p.pos.x-fe.x)**2 + (p.pos.y-fe.y)**2 + ((p.pos.z-fe.z2)*4)**2
            }.map { |p|
                [p.pos.x, p.pos.y, p.pos.z]
            }
        end



        # check if an item is free to use
        def is_item_free(i, allow_nonempty=false)
            !i.flags.trader and     # merchant's item
            !i.flags.in_job and     # current job input
            !i.flags.construction and
            !i.flags.encased and
            !i.flags.removed and    # deleted object
            !i.flags.forbid and     # user forbidden (or dumped)
            !i.flags.in_chest and   # in infirmary (XXX dwarf owned items ?)
            (!i.flags.container or allow_nonempty or
             !i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainsItemst) }) and     # is empty
            (!i.flags.in_inventory or
             (!i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefUnitHolderst) and       # is not in an unit's inventory (ignore if it is simply hauled)
                 ir.unit_tg.inventory.find { |ii| ii.item == i and ii.mode != :Hauled } } and
              !i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainedInItemst) and
                 !is_item_free(ir.item_tg, true) })) and
            (!i.flags.in_building or !i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefBuildingHolderst) and    # is not part of a building construction materials
             ir.building_tg.contained_items.find { |bi| bi.use_mode == 2 and bi.item == i } }) and
            (!i.flags.on_ground or (!df.map_tile_at(i).designation.hidden and df.map_tile_at(i).designation.flow_size < 4))
        end


        def is_metal_ore(i)
            # mat_index => bool
            @metal_ore_cache ||= CacheHash.new { |mi| df.world.raws.inorganics[mi].flags[:METAL_ORE] }
            (i.kind_of?(Integer) and @metal_ore_cache[i]) or
            (i.kind_of?(DFHack::ItemBoulderst) and i.mat_type == 0 and @metal_ore_cache[i.mat_index])
        end

        def is_raw_coke(i)
            # mat_index => custom reaction name
            @raw_coke_cache ||= df.world.raws.reactions.inject({}) { |h, r|
                if r.reagents.length == 1 and r.reagents.find { |rr|
                    rr.kind_of?(DFHack::ReactionReagentItemst) and rr.item_type == :BOULDER and rr.mat_type == 0
                } and r.products.find { |rp|
                    rp.kind_of?(DFHack::ReactionProductItemst) and rp.item_type == :BAR and
                    mt = df.decode_mat(rp) and mt.material and mt.material.id == 'COAL'
                }
                        # XXX check input size vs output size ?
                    h.update r.reagents[0].mat_index => r.code
                else
                    h
                end
            }
            (i.kind_of?(Integer) and @raw_coke_cache[i]) or
            (i.kind_of?(DFHack::ItemBoulderst) and i.mat_type == 0 and @raw_coke_cache[i.mat_index])
        end

        def is_gypsum(i)
            # mat_index => bool
            @gypsum_cache ||= CacheHash.new { |mi| df.world.raws.inorganics[mi].material.reaction_class.include?('GYPSUM') }
            (i.kind_of?(Integer) and @gypsum_cache[i]) or
            (i.kind_of?(DFHack::ItemBoulderst) and i.mat_type == 0 and @gypsum_cache[i.mat_index])
        end

        # determine if we may be able to generate metal bars for this metal
        # may queue manager_jobs to do so
        # recursive (eg steel need pig_iron)
        # return the potential number of bars available (in dimensions, eg 1 bar => 150)
        def may_forge_bars(mat_index, div=1)
            # simple metal ore
            moc = CacheHash.new { |mi|
                df.world.raws.inorganics[mi].metal_ore.mat_index.include?(mat_index)
            }

            can_melt = df.world.items.other[:BOULDER].find_all { |i|
                is_metal_ore(i) and moc[i.mat_index] and is_item_free(i)
            }.length

            @ai.plan.map_veins.keys.find { |k|
                can_melt += @ai.plan.dig_vein(k) if moc[k]
            } if can_melt < WatchStock[:metal_ore] and @ai.plan.past_initial_phase 

            if can_melt > WatchStock[:metal_ore]
                return 4*150*(can_melt - WatchStock[:metal_ore])
            end


            # "make <mi> bars" customreaction
            df.world.raws.reactions.each { |r|
                # XXX choose best reaction from all reactions
                prod_mult = nil
                next unless r.products.find { |rp|
                    prod_mult = rp.product_dimension if rp.kind_of?(DFHack::ReactionProductItemst) and
                    rp.item_type == :BAR and rp.mat_type == 0 and rp.mat_index == mat_index
                }

                can_reaction = 30
                future = false
                if r.reagents.all? { |rr|
                    # XXX may queue forge reagents[1] even if we dont handle reagents[2]
                    next if not rr.kind_of?(DFHack::ReactionReagentItemst)
                    next if rr.item_type != :BAR and rr.item_type != :BOULDER
                    has = 0
                    df.world.items.other[rr.item_type].each { |i|
                        next if rr.mat_type != -1 and i.mat_type != rr.mat_type
                        next if rr.mat_index != -1 and i.mat_index != rr.mat_index
                        next if not is_item_free(i)
                        next if rr.reaction_class != '' and (!(mi = df.decode_mat(i)) or !mi.material or !mi.material.reaction_class.include?(rr.reaction_class))
                        next if rr.metal_ore != -1 and i.mat_type == 0 and !df.world.raws.inorganics[i.mat_index].metal_ore.mat_index.include?(rr.metal_ore)
                        if rr.item_type == :BAR
                            has += i.dimension
                        else
                            has += 1
                        end
                    }
                    if has <= 0 and rr.item_type == :BOULDER and rr.mat_type == 0 and rr.mat_index != -1 and @ai.plan.past_initial_phase 
                        has += @ai.plan.dig_vein(rr.mat_index)
                        future = true if has > 0
                    end
                    has /= rr.quantity

                    if has <= 0 and rr.item_type == :BAR and rr.mat_type == 0 and rr.mat_index != -1
                        future = true
                        # 'div' tries to ensure that eg making pig iron wont consume all available iron
                        # and leave some to make steel
                        has = may_forge_bars(rr.mat_index, div+1)
                        next if not has
                    end

                    can_reaction = has/div if can_reaction > has/div

                    true
                }
                    next if can_reaction <= 0

                    if not future and not df.world.manager_orders.find { |mo|
                        mo.job_type == :CustomReaction and mo.reaction_name == r.code
                    }
                        @ai.debug "stocks: queue #{can_reaction} #{r.code}"
                        df.world.manager_orders << DFHack::ManagerOrder.cpp_new(:job_type => :CustomReaction, :unk_2 => -1,
                            :item_subtype => -1, :reaction_name => r.code, :mat_type => -1, :mat_index => -1,
                            :amount_left => can_reaction, :amount_total => can_reaction)
                    end
                    return prod_mult * can_reaction
                end
            }
            nil
        end

        ManagerRealOrder = {
            :BrewDrinkPlant => :CustomReaction,
            :BrewDrinkFruit => :CustomReaction,
            :BrewMead => :CustomReaction,
            :ProcessPlantsBag => :CustomReaction,
            :MakeSoap => :CustomReaction,
            :MakePlasterPowder => :CustomReaction,
            :PressHoneycomb => :CustomReaction,
            :MakeBag => :ConstructChest,
            :MakeRope => :MakeChain,
            :MakeWoodenWheelbarrow => :MakeTool,
            :MakeWoodenMinecart => :MakeTool,
            :MakeRockNestbox => :MakeTool,
            :MakeRockHive => :MakeTool,
            :MakeRockJug => :MakeTool,
            :MakeBoneBolt => :MakeAmmo,
            :MakeBoneCrossbow => :MakeWeapon,
            :MakeTrainingAxe => :MakeWeapon,
            :MakeTrainingShortSword => :MakeWeapon,
            :MakeTrainingSpear => :MakeWeapon,
            :MakeGiantCorkscrew => :MakeTrapComponent,
            :ConstructWoodenBlocks => :ConstructBlocks,
            :MakeWoodenStepladder => :MakeTool,
            :DecorateWithShell => :DecorateWith,
        }
        ManagerMatCategory = {
            :MakeRope => :cloth, :MakeBag => :cloth,
            :ConstructBed => :wood, :MakeBarrel => :wood, :MakeBucket => :wood, :ConstructBin => :wood,
            :MakeWoodenWheelbarrow => :wood, :MakeWoodenMinecart => :wood, :MakeTrainingAxe => :wood,
            :MakeTrainingShortSword => :wood, :MakeTrainingSpear => :wood,
            :ConstructCrutch => :wood, :ConstructSplint => :wood, :MakeCage => :wood,
            :MakeGiantCorkscrew => :wood, :MakePipeSection => :wood, :ConstructWoodenBlocks => :wood,
            :MakeBoneBolt => :bone, :MakeBoneCrossbow => :bone,
            :MakeQuiver => :leather, :MakeFlask => :leather, :MakeBackpack => :leather,
            :MakeWoodenStepladder => :wood, :DecorateWithShell => :shell,
        }
        ManagerType = {   # no MatCategory => mat_type = 0 (ie generic rock), unless specified here
            :ProcessPlants => -1, :ProcessPlantsBag => -1, :MillPlants => -1, :BrewDrinkPlant => -1,
            :ConstructTractionBench => -1, :MakeSoap => -1, :MakeLye => -1, :MakeAsh => -1,
            :MakeTotem => -1, :MakeCharcoal => -1, :MakePlasterPowder => -1, :PrepareMeal => 4,
            :DyeCloth => -1, :MilkCreature => -1, :PressHoneycomb => -1, :BrewDrinkFruit => -1,
            :BrewMead => -1, :MakeCheese => -1,
        }
        ManagerCustom = {
            :ProcessPlantsBag => 'PROCESS_PLANT_TO_BAG',
            :BrewDrinkPlant => 'BREW_DRINK_FROM_PLANT',
            :BrewDrinkFruit => 'BREW_DRINK_FROM_PLANT_GROWTH',
            :BrewMead => 'MAKE_MEAD',
            :MakeSoap => 'MAKE_SOAP_FROM_TALLOW',
            :MakePlasterPowder => 'MAKE_PLASTER_POWDER',
            :PressHoneycomb => 'PRESS_HONEYCOMB',
        }
        ManagerSubtype = {
            # depends on raws.itemdefs, wait until a world is loaded
        }

        def self.init_manager_subtype
            ManagerSubtype.update \
                :MakeWoodenWheelbarrow => df.world.raws.itemdefs.tools.find { |d| d.tool_use.include?(:HEAVY_OBJECT_HAULING) }.subtype,
                :MakeWoodenMinecart => df.world.raws.itemdefs.tools.find { |d| d.tool_use.include?(:TRACK_CART) }.subtype,
                :MakeRockNestbox => df.world.raws.itemdefs.tools.find { |d| d.tool_use.include?(:NEST_BOX) }.subtype,
                :MakeRockHive => df.world.raws.itemdefs.tools.find { |d| d.tool_use.include?(:HIVE) }.subtype,
                :MakeRockJug => df.world.raws.itemdefs.tools.find { |d| d.tool_use.include?(:LIQUID_CONTAINER) }.subtype,
                :MakeTrainingAxe => df.world.raws.itemdefs.weapons.find { |d| d.id == 'ITEM_WEAPON_AXE_TRAINING' }.subtype,
                :MakeTrainingShortSword => df.world.raws.itemdefs.weapons.find { |d| d.id == 'ITEM_WEAPON_SWORD_SHORT_TRAINING' }.subtype,
                :MakeTrainingSpear => df.world.raws.itemdefs.weapons.find { |d| d.id == 'ITEM_WEAPON_SPEAR_TRAINING' }.subtype,
                :MakeGiantCorkscrew => df.world.raws.itemdefs.trapcomps.find { |d| d.id == 'ITEM_TRAPCOMP_ENORMOUSCORKSCREW' }.subtype,
                :MakeBoneBolt => df.world.raws.itemdefs.ammo.find { |d| d.id == 'ITEM_AMMO_BOLTS' }.subtype,
                :MakeBoneCrossbow => df.world.raws.itemdefs.weapons.find { |d| d.id == 'ITEM_WEAPON_CROSSBOW' }.subtype,
                :MakeWoodenStepladder => df.world.raws.itemdefs.tools.find { |d| d.tool_use.include?(:STAND_AND_WORK_ABOVE) }.subtype
        end

        df.onstatechange_register { |st|
            init_manager_subtype if st == :WORLD_LOADED
        }
        init_manager_subtype if not df.world.raws.itemdefs.ammo.empty?

        def find_manager_orders(order)
            _order = ManagerRealOrder[order] || order
            matcat = ManagerMatCategory[order]
            type   = ManagerType[order]
            subtype= ManagerSubtype[order]
            custom = ManagerCustom[order]

            df.world.manager_orders.find_all { |_o|
                _o.job_type == _order and
                _o.mat_type == (type || (matcat ? -1 : 0)) and
                (matcat ? _o.material_category.send(matcat) : _o.material_category._whole == 0) and
                (not subtype or subtype == _o.item_subtype) and
                (not custom or custom == _o.reaction_name)
            }
        end

        # return the number of current manager orders that share the same material (leather, cloth)
        # ignore inorganics, ignore order
        def count_manager_orders_matcat(matcat, order=nil)
            cnt = 0
            df.world.manager_orders.each { |_o|
                cnt += _o.amount_total if _o.material_category.send(matcat) and _o.job_type != order
            }
            cnt
        end

        def add_manager_order(order, amount=1, maxmerge=30)
            @ai.debug "add_manager #{order} #{amount}"
            _order = ManagerRealOrder[order] || order
            matcat = ManagerMatCategory[order]
            type = ManagerType[order]
            subtype = ManagerSubtype[order]
            custom = ManagerCustom[order]

            if not o = find_manager_orders(order).find { |_o| _o.amount_total+amount <= maxmerge }
                # try to merge with last manager_order, upgrading maxmerge to 30
                o = df.world.manager_orders.last
                if o and o.job_type == _order and
                        o.amount_total + amount < 30 and
                        o.mat_type == (type || (matcat ? -1 : 0)) and
                        (matcat ? o.material_category.send(matcat) : o.material_category._whole == 0) and
                        (not subtype or subtype == o.item_subtype) and
                        (not custom or custom == o.reaction_name) and
                        o.amount_total + amount < 30
                    o.amount_total += amount
                    o.amount_left += amount
                else
                    o = DFHack::ManagerOrder.cpp_new(:job_type => _order, :unk_2 => -1, :item_subtype => (subtype || -1),
                            :mat_type => (type || (matcat ? -1 : 0)), :mat_index => -1, :amount_left => amount, :amount_total => amount)
                    o.material_category.send("#{matcat}=", true) if matcat
                    o.reaction_name = custom if custom
                    o.mat_index = df.decode_mat('INORGANIC:RAW_ADAMANTINE').mat_index if _order == :ExtractMetalStrands
                    df.world.manager_orders << o
                end
            else
                o.amount_total += amount
                o.amount_left += amount
            end
        end

        FurnitureOrder = Hash.new { |h, k|
            h[k] = "Construct#{k.to_s.capitalize}".to_sym
        }.update :chair => :ConstructThrone,
            :traction_bench => :ConstructTractionBench,
            :weaponrack => :ConstructWeaponRack,
            :armorstand => :ConstructArmorStand,
            :bucket => :MakeBucket,
            :barrel => :MakeBarrel,
            :bin => :ConstructBin,
            :crutch => :ConstructCrutch,
            :splint => :ConstructSplint,
            :bag => :MakeBag,
            :block => :ConstructBlocks,
            :mechanism => :ConstructMechanisms,
            :trap => :ConstructMechanisms,
            :cage => :MakeCage,
            :soap => :MakeSoap,
            :rope => :MakeRope,
            :lye => :MakeLye,
            :ash => :MakeAsh,
            :plasterpowder => :MakePlasterPowder,
            :wheelbarrow => :MakeWoodenWheelbarrow,
            :minecart => :MakeWoodenMinecart,
            :nestbox => :MakeRockNestbox,
            :hive => :MakeRockHive,
            :jug => :MakeRockJug,
            :quiver => :MakeQuiver,
            :flask => :MakeFlask,
            :backpack => :MakeBackpack,
            :giant_corkscrew => :MakeGiantCorkscrew,
            :pipe_section => :MakePipeSection,
            :coal => :MakeCharcoal,
            :stepladder => :MakeWoodenStepladder

        FurnitureFind = Hash.new { |h, k|
            sym = "item_#{k}st".to_sym
            h[k] = lambda { |o| o._rtti_classname == sym }
        }.update :chest => lambda { |o| o._rtti_classname == :item_boxst and o.mat_type == 0 },
                 :hive => lambda { |o| o._rtti_classname == :item_toolst and o.subtype.subtype == ManagerSubtype[FurnitureOrder[:hive]] },
                 :nestbox => lambda { |o| o._rtti_classname == :item_toolst and o.subtype.subtype == ManagerSubtype[FurnitureOrder[:nestbox]] },
                 :trap => lambda { |o| o._rtti_classname == :item_trappartsst },
                 :stepladder => lambda { |o| o._rtti_classname == :item_toolst and o.subtype.subtype == ManagerSubtype[FurnitureOrder[:stepladder]] }

        # find one item of this type (:bed, etc)
        def find_furniture_item(itm)
            find = FurnitureFind[itm]
            oidx = DFHack::JobType::Item.fetch(FurnitureOrder[itm],
                    DFHack::JobType::Item.fetch(ManagerRealOrder[FurnitureOrder[itm]], :IN_PLAY))
            df.world.items.other[oidx].find { |i| find[i] and df.item_isfree(i) }
        end

        # return nr of free items of this type
        def find_furniture_itemcount(itm)
            find = FurnitureFind[itm]
            oidx = DFHack::JobType::Item.fetch(FurnitureOrder[itm],
                    DFHack::JobType::Item.fetch(ManagerRealOrder[FurnitureOrder[itm]], :IN_PLAY))
            df.world.items.other[oidx].find_all { |i| find[i] and df.item_isfree(i) }.length
        rescue
            puts_err "df-ai stocks: cannot itemcount #{itm.inspect}", $!, $!.backtrace
            0
        end

        def cutting_trees?
            return false if not @last_cutpos
            return true if t = df.map_tile_at(*@last_cutpos) and t.designation and t.designation.dig == :Default

            if tree = tree_list.find { |t|
                tile = df.map_tile_at(*t) and tile.designation and tile.designation.dig == :Default
            }
                @last_cutpos = tree
                return true
            end
            @last_cutpos = nil
            false
        end

        def farmplot(r, initial=true)
            return unless bld = r.dfbuilding

            biomes = Hash.new(0)
            (bld.x1..bld.x2).each do |x|
                (bld.y1..bld.y2).each do |y|
                    td = df.map_tile_at(x, y, bld.z).designation
                    if td.subterranean
                        biomes[:BIOME_SUBTERRANEAN_WATER] += 1
                    else
                        biomes[:"BIOME_#{DFHack::BiomeType::ENUM[td.biome]}"] += 1
                    end
                end
            end
            if biomes.length != 1
                puts "AI: multiple biomes for #{r.subtype} farm plot #{ai.plan.rooms.index(r)}: #{biomes}"
                return true # we can't farm here
            end
            biome = biomes.keys.first

            may = []
            df.world.raws.plants.all.length.times { |i|
                p = df.world.raws.plants.all[i]
                next if not p.flags[biome]
                next if p.flags[:TREE]
                may << i
            }

            # XXX 1st plot = the one with a door
            isfirst = !r.layout.empty?
            if r.subtype == :food
                4.times { |season|
                    pids = may.find_all { |i|
                        p = df.world.raws.plants.all[i]

                        # season numbers are also the 1st 4 flags
                        next if not p.flags[season]

                        pm = df.decode_mat(p.material_defs.type_basic_mat, p.material_defs.idx_basic_mat).material
                        if isfirst
                            pm.flags[:EDIBLE_RAW] and p.flags[:DRINK]
                        else
                            pm.flags[:EDIBLE_RAW] or pm.flags[:EDIBLE_COOKED] or p.flags[:DRINK] or
                            (p.flags[:MILL] and mm = df.decode_mat(p.material_defs.type_mill, p.material_defs.idx_mill).material and (mm.flags[:EDIBLE_RAW] or mm.flags[:EDIBLE_COOKED])) or
                            (bi = pm.reaction_product.id.index('BAG_ITEM') and bm = df.decode_mat(pm.reaction_product.material.mat_type[bi], pm.reaction_product.material.mat_index[bi]).material and (bm.flags[:EDIBLE_RAW] or bm.flags[:EDIBLE_COOKED]))
                        end
                    }.sort_by { |i|
                        @plants[i] - @seeds[i] + 18 * @farmplots[[season, i]]
                    }

                    if pids.empty?
                        @complained_about_no_plants ||= {}
                        puts "AI: no legal plants for #{r.subtype} farm plot #{ai.plan.rooms.index(r)} in #{biome} season #{season}" unless @complained_about_no_plants[[r.subtype, biome, season]]
                        @complained_about_no_plants[[r.subtype, biome, season]] = true unless isfirst
                    else
                        @farmplots[[season, bld.plant_id[season]]] -= 1 unless initial
                        bld.plant_id[season] = pids.first
                        @farmplots[[season, pids.first]] += 1 unless initial
                    end
                }
            else
                threads = may.find_all { |i|
                    p = df.world.raws.plants.all[i]
                    p.flags[:THREAD]
                }
                dyes = may.find_all { |i|
                    p = df.world.raws.plants.all[i]
                    p.flags[:MILL] and df.decode_mat(p.material_defs.type_mill, p.material_defs.idx_mill).material.flags[:IS_DYE]
                }

                4.times { |season|
                    pids = threads.find_all { |i|
                        p = df.world.raws.plants.all[i]
                        p.flags[season]
                    }
                    # only grow dyes the first field if there is no cloth crop available
                    if pids.empty? or !isfirst
                        pids |= dyes.find_all { |i|
                            p = df.world.raws.plants.all[i]
                            p.flags[season]
                        }
                    end

                    pids = pids.sort_by { |i|
                        @plants[i] - @seeds[i] + 18 * @farmplots[[season, i]]
                    }

                    if pids.empty?
                        @complained_about_no_plants ||= {}
                        puts "AI: no legal plants for #{r.subtype} farm plot #{rooms.index(r)} in #{biome} season #{season}" unless @complained_about_no_plants[[r.subtype, biome, season]]
                        @complained_about_no_plants[[r.subtype, biome, season]] = true unless isfirst
                    else
                        @farmplots[[season, bld.plant_id[season]]] -= 1 unless initial
                        bld.plant_id[season] = pids.first
                        @farmplots[[season, pids.first]] += 1 unless initial
                    end
                }

                # TODO repurpose fields if we have too much dimple dye or smth
            end
        end

        def serialize
            {} # we don't need to save any state
        end
    end
end

# vim: et:sw=4:ts=4
