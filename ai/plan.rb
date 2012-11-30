class DwarfAI
    class Plan
        attr_accessor :ai, :tasks
        attr_accessor :manager_taskmax, :manager_maxbacklog,
            :dwarves_per_table, :dwarves_per_farmtile,
            :wantdig_max, :spare_bedroom
        attr_accessor :onupdate_handle

        def initialize(ai)
            @ai = ai
            @nrdig = 0
            @tasks = []
            @tasks << [:checkrooms]
            @rooms = []
            @corridors = []
            @manager_taskmax = 4    # when stacking manager jobs, do not stack more than this
            @manager_maxbacklog = 10 # add new masonly if more that this much mason manager orders
            @dwarves_per_table = 3  # number of dwarves per dininghall table/chair
            @dwarves_per_farmtile = 1.5   # number of dwarves per farmplot tile
            @wantdig_max = 2    # dig at most this much wantdig rooms at a time
            @spare_bedroom = 3  # dig this much free bedroom in advance when idle
            categorize_all
        end

        def debug(str)
            puts "AI: #{df.cur_year}:#{df.cur_year_tick} #{str}" if $DEBUG
        end

        def startup
            setup_blueprint
            categorize_all

            ensure_workshop(:Masons)
            ensure_workshop(:Carpenters)
            
            dig_garbagedump
        end

        def onupdate_register
            @onupdate_handle = df.onupdate_register('df-ai plan', 240, 20) { update }
        end

        def onupdate_unregister
            df.onupdate_unregister(@onupdate_handle)
        end

        def update
            @bg_idx ||= 0
            if @bg_idx < @tasks.length
                if @last_bg_idx != @bg_idx
                    @last_bg_idx = @bg_idx
                    return
                end
            end
            @bg_idx = 0

            @cache_nofurnish = {}

            @nrdig = @tasks.count { |t| t[0] == :digroom and (t[1].type != :corridor or t[1].h_z>1) }
            @nrdig += @tasks.count { |t| t[0] == :digroom and t[1].type != :corridor and t[1].w*t[1].h*t[1].h_z >= 10 }

            # avoid too much manager backlog
            manager_backlog = df.world.manager_orders.find_all { |o| o.mat_type == 0 and o.mat_index == -1 }.length
            want_reupdate = false
            bg = df.onupdate_register('df-ai plan bg') {
                t = @tasks[@bg_idx]
                if not t
                    update if want_reupdate
                    df.onupdate_unregister(bg)
                    next
                end

                bg.description = "df-ai plan bg #{t[0]} #{t[1].type if t[1].kind_of?(Corridor)}"
                del = case t[0]
                when :wantdig
                    digroom(t[1]) if t[1].dug? or @nrdig<@wantdig_max
                when :digroom
                    if t[1].dug?
                        t[1].status = :dug
                        construct_room(t[1])
                        want_reupdate = true    # wantdig asap
                        true
                    end
                when :construct_workshop
                    try_construct_workshop(t[1])
                when :setup_farmplot
                    try_setup_farmplot(t[1])
                when :furnish
                    try_furnish(t[1], t[2])
                when :checkfurnish
                    try_endfurnish(t[1], t[2])
                when :checkconstruct
                    try_endconstruct(t[1])
                when :dig_cistern
                    try_digcistern(t[1])
                when :dig_garbage
                    try_diggarbage(t[1])
                when :checkidle
                    checkidle
                when :checkrooms
                    checkrooms
                    false
                when :monitor_cistern
                    monitor_cistern
                    false
                end

                if del
                    @tasks.delete_at(@bg_idx)
                else
                    @bg_idx += 1
                end
            }
        end

        def digging?
            @tasks.find { |t| (t[0] == :wantdig or t[0] == :digroom) and t[1].type != :corridor }
        end

        def idle?
            @tasks.reject { |t|
                case t[0]
                when :monitor_cistern, :checkrooms, :checkidle
                    true
                end
            }.empty?
        end

        def new_citizen(uid)
            @tasks << [:checkidle] unless @tasks.find { |t| t[0] == :checkidle }
            getdiningroom(uid)
            getbedroom(uid)
        end

        def del_citizen(uid)
            freecommonrooms(uid)
            freebedroom(uid)
        end
        
        def checkidle
            return if digging?

            df.world.buildings.other[:WAGON].each { |w|
                if w.kind_of?(DFHack::BuildingWagonst) and not w.contained_items.find { |i| i.use_mode == 0 }
                    df.building_deconstruct(w)
                end
            }

            @important_workshops ||= [:Mechanics, :Butchers, :Craftsdwarfs, :Kitchen, :Tanners, :Farmers, :WoodFurnace, :Loom, :Smelter]

            # if nothing better to do, order the miners to dig remaining stockpiles, workshops, and a few bedrooms
            manager_backlog = df.world.manager_orders.find_all { |o| o.mat_type == 0 and o.mat_index == -1 }.length

            ifplan = lambda { |_r| _r if _r and _r.status == :plan }
            freebed = @spare_bedroom
            if manager_backlog >= @manager_maxbacklog and r = find_room(:workshop) { |_r| not _r.subtype and _r.status == :plan }
                r.misc[:spare] = true
                r.subtype = :Masons # TODO repurpose by parsing manager_orders
                digroom(r)
                false
            elsif r =
                   (st = @important_workshops.shift and
                    find_room(:workshop) { |_r| _r.subtype == st and _r.status == :plan }) ||
                   find_room(:infirmary) { |_r| _r.status == :plan } ||
                   find_room(:workshop)  { |_r| _r.subtype and _r.status == :plan } ||
                   find_room(:cistern)   { |_r| _r.subtype == :well and _r.status == :plan } ||
                   (@fort_entrance if not @fort_entrance.misc[:furnished]) ||
                   find_room(:stockpile) { |_r| not _r.misc[:secondary] and _r.subtype and _r.status == :plan } ||
                   find_room(:bedroom)   { |_r| not _r.owner and ((freebed -= 1) >= 0) and _r.status == :plan } ||
                   find_room(:nobleroom) { |_r| _r.status == :finished and not _r.misc[:furnished] } ||
                   find_room(:bedroom)   { |_r| _r.status == :finished and not _r.misc[:furnished] } ||
                   ifplan[find_room(:dininghall) { |_r| _r.layout.find { |f| f[:users] and f[:users].empty? } }] ||
                   ifplan[find_room(:barracks)   { |_r| _r.layout.find { |f| f[:users] and f[:users].empty? } }] ||
                   find_room(:stockpile) { |_r| _r.subtype and _r.status == :plan }
                wantdig(r)
                if r.status == :finished
                    r.misc[:furnished] = true
                    r.layout.each { |f| f.delete :ignore }
                    furnish_room(r)
                    smooth_room(r)
                end
                false
            elsif idle?
                idleidle
                true
            end
        end

        def idleidle
            debug 'smooth fort'
            tab = []
            @rooms.each { |r|
                next if r.status == :plan or r.status == :dig

                case r.type
                when :nobleroom, :bedroom, :well, :dininghall, :cemetary, :infirmary, :barracks
                    tab << r
                end
            }
            @corridors.each { |r|
                next if r.status == :plan or r.status == :dig
                tab << r
            }

            df.onupdate_register_once('df-ai plan idleidle', 4) {
                if r = tab.shift
                    smooth_room(r)
                    r.layout.each { |f| build_furniture(f, true) }
                    false
                else
                    true
                end
            }

            ai.pop.worker_labor.each { |w, wl|
                df.unit_find(w).status.labors[:DETAIL] = true
            }
        end

        def checkrooms
            @checkroom_idx ||= 0
            ncheck = 4
            (ncheck*4).times {
                r = @corridors[@checkroom_idx]
                if r and r.status != :plan
                    checkroom(r)
                end

                r = @rooms[@checkroom_idx]
                if r and r.status != :plan
                    checkroom(r)
                    ncheck -= 1
                    break if ncheck <= 0
                end
                @checkroom_idx += 1
            }
            @checkroom_idx = 0 if @checkroom_idx >= @rooms.length and @checkroom_idx >= @corridors.length
        end

        # ensure room was not tantrumed etc
        def checkroom(r)
            case r.status
            when :plan
                # moot
            when :dig
                # designation cancelled: damp stone etc
                r.dig
            when :dug, :finished
                # cavein / tree
                r.dig
                # tantrumed furniture
                r.layout.each { |f|
                    next if f[:ignore]
                    if f[:bld_id] and not df.building_find(f[:bld_id])
                        df.add_announcement("AI: fix furniture #{f[:item]} in #{r.type} #{r.subtype}", 7, false) { |ann| ann.pos = [r.x1+f[:x].to_i, r.y1+f[:y].to_i, r.z1+f[:z].to_i] }
                        f.delete :bld_id
                        f.delete :queue_build
                        @tasks << [:furnish, r, f]
                    end
                }
                # tantrumed building
                if r.misc[:bld_id] and not r.dfbuilding
                    df.add_announcement("AI: rebuild #{r.type} #{r.subtype}", 7, false) { |ann| ann.pos = r }
                    r.misc.delete :bld_id
                    construct_room(r)
                end
            end
        end

        def getbedroom(id)
            if r = find_room(:bedroom) { |_r| not _r.owner or _r.owner == id } ||
                   find_room(:bedroom) { |_r| _r.status == :plan and not _r.misc[:queue_dig] }
                wantdig(r)
                set_owner(r, id)
                name = ((u = df.unit_find(id)) ? u.name : '?')
                df.add_announcement("AI: assigned a bedroom to #{name}", 7, false) { |ann| ann.pos = r }
                if r.status == :finished
                    furnish_room(r)
                end
            else
                puts "AI cant getbedroom(#{id})"
            end
        end

        def getdiningroom(id)
            if r = find_room(:farmplot) { |_r| _r.subtype == :food and _r.misc[:users].length < _r.w*_r.h*@dwarves_per_farmtile }
                wantdig(r)
                r.misc[:users] << id
            end
            if r = find_room(:farmplot) { |_r| _r.subtype == :cloth and _r.misc[:users].length < _r.w*_r.h*@dwarves_per_farmtile }
                wantdig(r)
                r.misc[:users] << id
            end

            if r = find_room(:dininghall) { |_r| _r.layout.find { |f| f[:users] and f[:users].length < @dwarves_per_table } }
                wantdig(r)
                table = r.layout.find { |f| f[:item] == :table and f[:users].length < @dwarves_per_table }
                chair = r.layout.find { |f| f[:item] == :chair and f[:users].length < @dwarves_per_table }
                table.delete :ignore
                chair.delete :ignore
                table[:users] << id
                chair[:users] << id
                if r.status == :finished
                    furnish_room(r)
                end
            end
        end

        def getsoldierbarrack(id)
            u = df.unit_find(id)
            return if not u
            squad_id = u.military.squad_id
            return if squad_id == -1

            if not r = find_room(:barracks) { |_r| _r.misc[:squad_id] == squad_id }
                r = find_room(:barracks) { |_r| not _r.misc[:squad_id] }
                if not r
                    puts "AI: no free barracks"
                    return
                end

                r.misc[:squad_id] = squad_id
                df.add_announcement("AI: new barracks #{r.misc[:squad_id]}", 7, false) { |ann| ann.pos = r }
                wantdig(r)
                if r.misc[:bld_id]
                    assign_barrack_squad(r, squad_id)
                end
            end

            [:weaponrack, :armorstand, :bed, :cabinet, :chest].map { |it|
                r.layout.find { |f| f[:item] == it and f[:users].include?(id) } ||
                r.layout.find { |f| f[:item] == it and f[:users].length < (it == :weaponrack ? 4 : 1) }
            }.compact.each { |f|
                f[:users] << id unless f[:users].include?(id)
                f.delete :ignore
            }
            if r.status == :finished
                furnish_room(r)
            end
        end
        
        def assign_barrack_squad(barrack, squad_id)
            return if not bld = barrack.dfbuilding

            su = bld.squads.find { |_su| _su.squad_id == squad_id }
            if not su
                su = DFHack::BuildingSquadUse.cpp_new(:squad_id => squad_id)
                bld.squads << su
            end
            su.mode.sleep = true
            su.mode.train = true
            su.mode.indiv_eq = true
            su.mode.squad_eq = true

            squad = df.world.squads.all.binsearch(squad_id)
            sr = squad.rooms.find { |_sr| _sr.building_id == bld.id }
            if not sr
                sr = DFHack::Squad_TRooms.cpp_new(:building_id => bld.id)
                squad.rooms << sr
            end
            sr.mode.sleep = true
            sr.mode.train = true
            sr.mode.indiv_eq = true
            sr.mode.squad_eq = true
        end

        def getcoffin
            if r = find_room(:cemetary) { |_r|
                    _r.layout.find { |f| f[:users] and f[:users].length < 1 }
            }
                wantdig(r)
                coffin = r.layout.find { |f| f[:item] == :coffin and f[:users].length < 1 }
                coffin[:users] << 0
                coffin.delete :ignore
                if r.status == :finished
                    furnish_room(r)
                end
            end
        end

        # free / deconstruct the bedroom assigned to this dwarf
        def freebedroom(id)
            if r = find_room(:bedroom) { |_r| _r.owner == id }
                name = ((u = df.unit_find(id)) ? u.name : '?')
                df.add_announcement("AI: freed bedroom of #{name}", 7, false) { |ann| ann.pos = r }
                set_owner(r, nil)
                r.layout.each { |f|
                    next if f[:ignore]
                    next if f[:item] == :door
                    if f[:bld_id] and bld = df.building_find(f[:bld_id])
                        df.building_deconstruct(bld)
                        f.delete :bld_id
                    end
                }
                r.misc.delete(:bld_id)
            end
        end

        # free / deconstruct the common facilities assigned to this dwarf
        # optionnaly restricted to a single subtype among:
        #  [dininghall, farmplots, barracks]
        def freecommonrooms(id, subtype=nil)
            @rooms.each { |r|
                next if subtype and subtype != r.type

                case r.type
                when :dininghall, :barracks
                    r.layout.each { |f|
                        next unless f[:users]
                        next if f[:ignore]
                        if f[:users].delete(id) and f[:users].empty?
                            # delete the specific table/chair/bed/etc for the dwarf
                            if f[:bld_id] and f[:bld_id] != r.misc[:bld_id]
                                if bld = df.building_find(f[:bld_id])
                                    df.building_deconstruct(bld)
                                end
                                f.delete :bld_id
                                f[:ignore] = true
                            end

                            # clear the whole room if it is entirely unused
                            if r.misc[:bld_id] and r.layout.all? { |f| f[:users].to_a.empty? }
                                if bld = r.dfbuilding
                                    df.building_deconstruct(bld)
                                end
                                r.misc.delete(:bld_id)

                                if r.misc[:squad_id]
                                    df.add_announcement("AI: freed barracks #{r.misc[:squad_id]}", 7, false) { |ann| ann.pos = r }
                                    r.misc.delete :squad_id
                                end
                            end
                        end
                    }

                when :farmplot
                    r.misc[:users].delete id
                    if r.misc[:bld_id] and r.misc[:users].empty?
                        if bld = r.dfbuilding
                            df.building_deconstruct(bld)
                        end
                        r.misc.delete(:bld_id)
                    end

                end
            }
        end

        def freesoldierbarrack(id)
            freecommonrooms(id, :barracks)
        end

        def getpasture(pet_id)
            pet_per_pasture = 3     # TODO tweak by appetite ?
            if r = find_room(:pasture) { |_r| _r.misc[:users].length < pet_per_pasture }
                r.misc[:users] << pet_id
                construct_room(r) if not r.misc[:bld_id]
                r.dfbuilding
            end
        end

        def freepasture(pet_id)
            if r = find_room(:pasture) { |_r| _r.misc[:users].include?(pet_id) }
                r.misc[:users].delete pet_id
            end
        end

        def set_owner(r, uid)
            r.owner = uid
            if r.misc[:bld_id]
                u = df.unit_find(uid) if uid
                if bld = r.dfbuilding
                    df.building_setowner(bld, u)
                end
            end
        end

        # queue a room for digging when other dig jobs are finished
        def wantdig(r)
            return true if r.misc[:queue_dig] or r.status != :plan
            debug "wantdig #{@rooms.index(r)} #{r.type} #{r.subtype}"
            r.misc[:queue_dig] = true
            r.layout.each { |f| build_furniture(f) if f[:makeroom] }
            r.dig(:plan)
            @tasks << [:wantdig, r]
        end

        def digroom(r)
            return true if r.status != :plan
            debug "digroom #{@rooms.index(r)} #{r.type} #{r.subtype}"
            r.misc.delete :queue_dig
            r.status = :dig
            r.dig
            @tasks << [:digroom, r]
            r.accesspath.each { |ap| digroom(ap) }

            r.layout.each { |f|
                build_furniture(f)
            }

            if r.type == :workshop
                case r.subtype
                when :Quern
                    add_manager_order(:ConstructQuern)
                end

                # add minimal stockpile in front of workshop
                if sptype = {:Masons => :stone, :Carpenters => :wood, :Craftsdwarfs => :refuse,
                        :Farmers => :food, :Fishery => :food, :Jewelers => :gems, :Loom => :cloth,
                        :Clothiers => :cloth, :Still => :food, :Kitchen => :food, :WoodFurnace => :wood,
                        :Smelter => :stone, :MetalsmithsForge => :bars_blocks,
                }[r.subtype]
                    # XXX hardcoded fort layout
                    y = (r.layout[0][:y] > 0 ? r.y2+2 : r.y1-2)  # check door position
                    sp = Room.new(:stockpile, sptype, r.x1, r.x2, y, y, r.z1)
                    sp.misc[:workshop] = r
                    @rooms << sp
                    digroom(sp)
                end
            end

            if r.type == :cistern and r.subtype == :well
                # preorder wall smoothing to speedup floor channeling
                ((r.z1+1)..r.z2).each { |z|
                    ((r.x1-1)..(r.x2+1)).each { |x|
                        ((r.y1-1)..(r.y2+1)).each { |y|
                            next if not t = df.map_tile_at(x, y, z) or t.designation.dig != :No
                            t.designation.smooth = 1
                        }
                    }
                }

                ai.pop.worker_labor.each { |w, wl|
                    df.unit_find(w).status.labors[:DETAIL] = !wl.include?(:MINE)
                }
            end

            if r.type != :corridor or r.h_z > 1
                @nrdig += 1
                @nrdig += 1 if r.w*r.h*r.h_z >= 10
            end

            true
        end

        def construct_room(r)
            debug "construct #{@rooms.index(r)} #{r.type} #{r.subtype}"
            case r.type
            when :corridor
                furnish_room(r)
            when :stockpile
                construct_stockpile(r)
            when :workshop
                @tasks << [:construct_workshop, r]
            when :farmplot
                construct_farmplot(r)
            when :cistern
                construct_cistern(r)
            when :cemetary
                furnish_room(r)
            when :infirmary, :pasture
                construct_activityzone(r)
            when :dininghall
                if t = find_room(:dininghall) { |_r| _r.misc[:temporary] } and not r.misc[:temporary]
                    move_dininghall_fromtemp(r, t)
                end
                furnish_room(r)
            else
                furnish_room(r)
            end
        end

        def furnish_room(r)
            r.layout.each { |f| @tasks << [:furnish, r, f] }
            r.status = :finished
        end

        FurnitureBuilding = Hash.new { |h, k| h[k] = k.to_s.capitalize.to_sym }.update :chest => :Box,
            :traction_bench => :TractionBench

        FurnitureWorkshop = Hash.new(:Masons).update :bed => :Carpenters,
            :traction_bench => :Mechanics

        FurnitureOrder = Hash.new { |h, k|
            h[k] = "Construct#{k.to_s.capitalize}".to_sym
        }.update :chair => :ConstructThrone,
            :traction_bench => :ConstructTractionBench,
            :weaponrack => :ConstructWeaponRack,
            :armorstand => :ConstructArmorStand,
            :bucket => :MakeBucket,
            :barrel => :MakeBarrel,
            :bin => :ConstructBin,
            :drink => :BrewDrink,
            :crutch => :ConstructCrutch,
            :splint => :ConstructSplint,
            :bag => :MakeBag,
            :rockblock => :ConstructBlocks,
            :mechanism => :ConstructMechanisms,
            :trap => :ConstructMechanisms,
            :cage => :MakeCage,
            :soap => :MakeSoap,
            :coal => :MakeCharcoal

        FurnitureFind = Hash.new { |h, k|
            h[k] = lambda { |o| o._rtti_classname == "item_#{k}st".to_sym }
        }.update :chest => lambda { |o| o._rtti_classname == :item_boxst and o.mat_type == 0 },
                 :trap => lambda { |o| o._rtti_classname == :item_trappartsst }

        def try_furnish(r, f)
            return true if f[:bld_id]
            return true if f[:ignore]
            build_furniture(f)
            tgtile = df.map_tile_at(r.x1+f[:x].to_i, r.y1+f[:y].to_i, r.z1+f[:z].to_i)
            case f[:item]
            when :well
                return try_furnish_well(r, f)
            when :pillar
                tgtile.dig(:Smooth)
                return true
            end

            return if @cache_nofurnish[f[:item]]
            mod = FurnitureOrder[f[:item]]
            find = FurnitureFind[f[:item]]
            oidx = DFHack::JobType::Item[mod]
            if tgtile.occupancy.building != :None
                # TODO warn if this stays for too long?
                false
            elsif tgtile.shape == :RAMP or tgtile.shape == :TREE
                tgtile.dig(:Default)
                false
            elsif itm = df.world.items.other[oidx].find { |i| find[i] and df.item_isfree(i) }
                debug "furnish #{@rooms.index(r)} #{r.type} #{r.subtype} #{f[:item]}"
                bldn = FurnitureBuilding[f[:item]]
                subtype = { :cage => :CageTrap, :lever => :Lever }.fetch(f[:subtype], -1)
                bld = df.building_alloc(bldn, subtype)
                df.building_position(bld, tgtile)
                df.building_construct(bld, [itm])
                if f[:makeroom]
                    r.misc[:bld_id] = bld.id
                end
                f[:bld_id] = bld.id
                @tasks << [:checkfurnish, r, f]
                true
            else
                @cache_nofurnish[f[:item]] = true
                false
            end
        end

        def try_furnish_well(r, f)
            if block = df.world.items.other[:BLOCKS].find { |i|
                i.kind_of?(DFHack::ItemBlocksst) and df.item_isfree(i)
            } and mecha = df.world.items.other[:TRAPPARTS].find { |i|
                i.kind_of?(DFHack::ItemTrappartsst) and df.item_isfree(i)
            } and bucket = df.world.items.other[:BUCKET].find { |i|
                i.kind_of?(DFHack::ItemBucketst) and df.item_isfree(i) and not i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainsItemst) }
            } and chain = df.world.items.other[:CHAIN].find { |i|
                i.kind_of?(DFHack::ItemChainst) and df.item_isfree(i)
            }
                bld = df.building_alloc(:Well)
                t = df.map_tile_at(r.x1+f[:x].to_i, r.y1+f[:y].to_i, r.z1+f[:z].to_i)
                df.building_position(bld, t)
                df.building_construct(bld, [block, mecha, bucket, chain])
                r.misc[:bld_id] = f[:bld_id] = bld.id
                @tasks << [:checkfurnish, r, f]
                @rooms.each { |_r| wantdig(_r) if _r.type == :cistern }
                true
            end
        end

        def try_furnish_trap(r, f)
            t = df.map_tile_at(r.x1+f[:x].to_i, r.y1+f[:y].to_i, r.z1+f[:z].to_i)

            if t.occupancy.building != :None
                return
            elsif t.shape == :RAMP or t.shape == :TREE
                # XXX dont remove last access ramp ?
                t.dig(:Default)
                return
            end

            if mecha = df.world.items.other[:TRAPPARTS].find { |i|
                i.kind_of?(DFHack::ItemTrappartsst) and df.item_isfree(i)
            }
                subtype = {:lever => :Lever, :cage => :CageTrap}.fetch(f[:subtype])
                bld = df.building_alloc(:Trap, subtype)
                df.building_position(bld, t)
                df.building_construct(bld, [mecha])
                f[:bld_id] = bld.id
                @tasks << [:checkfurnish, r, f]
                true
            end
        end

        def build_furniture(f, build_ignored = false)
            return if f[:queue_build]
            return if f[:ignore] and not build_ignored
            case f[:item]
            when :well
                add_manager_order(:MakeRope)
            when :pillar
            else
                ws = FurnitureWorkshop[f[:item]]
                mod = FurnitureOrder[f[:item]]
                #ensure_workshop(ws)
                add_manager_order(mod, 1, @manager_taskmax)
            end
            f[:queue_build] = true
        end

        def ensure_workshop(subtype, dignow=(subtype.to_s !~ /Office/))
            ws = find_room(:workshop) { |r| r.subtype == subtype }
            raise "unknown workshop #{subtype}" if not ws
            if ws.status == :plan
                if dignow
                    digroom(ws)
                elsif subtype == :ManagersOffice
                    wantdig(ws)
                    # population level may warrant a manager to manage work orders, and the AI need him for everything
                    try_count = 0
                    mo = df.world.manager_orders.find { |_mo| _mo.is_validated == 0 }
                    df.onupdate_register_once('df-ai plan ensure_workshop manager', 10) {
                        try_count += 1
                        if !mo or mo.is_validated == 1
                            true
                        elsif try_count > 20
                            # dig now, and ensure the throne is on its way
                            digroom(ws)
                            df.world.manager_orders.each { |_mo| _mo.is_validated = 1 if _mo.job_type == :ConstructThrone }
                            true
                        end
                    }
                else
                    wantdig(ws)
                end
            end
            ws
        end

        def ensure_stockpile(sptype)
            if sp = find_room(:stockpile) { |r| r.subtype == sptype and not r.misc[:workshop] }
                ensure_stockpile(:food) if sptype != :food  # XXX make sure we dig food first
                wantdig(sp)
            end
        end

        def try_construct_workshop(r)
            case r.subtype
            when :ManagersOffice, :BookkeepersOffice
                r.layout.each { |f|
                    @tasks << [:furnish, r, f] if f[:makeroom]
                }
                true
            when :Dyers
                # barrel, bucket
                if barrel = df.world.items.other[:BARREL].find { |i|
                        i.kind_of?(DFHack::ItemBarrelst) and df.item_isfree(i) and not i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainsItemst) }
                } and bucket = df.world.items.other[:BUCKET].find { |i|
                        i.kind_of?(DFHack::ItemBucketst) and df.item_isfree(i) and not i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainsItemst) }
                }
                    bld = df.building_alloc(:Workshop, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [barrel, bucket])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :Ashery
                # block, barrel, bucket
                if block = df.world.items.other[:BLOCKS].find { |i|
                        i.kind_of?(DFHack::ItemBlocksst) and df.item_isfree(i)
                } and barrel = df.world.items.other[:BARREL].find { |i|
                        i.kind_of?(DFHack::ItemBarrelst) and df.item_isfree(i) and not i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainsItemst) }
                } and bucket = df.world.items.other[:BUCKET].find { |i|
                        i.kind_of?(DFHack::ItemBucketst) and df.item_isfree(i) and not i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainsItemst) }
                }
                    bld = df.building_alloc(:Workshop, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [block, barrel, bucket])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :SoapMaker
                # bucket, boulder
                if bucket = df.world.items.other[:BUCKET].find { |i|
                        i.kind_of?(DFHack::ItemBucketst) and df.item_isfree(i) and not i.general_refs.find { |ir| ir.kind_of?(DFHack::GeneralRefContainsItemst) }
                } and bould = df.world.items.other[:BOULDER].find { |i|
                        i.kind_of?(DFHack::ItemBoulderst) and df.item_isfree(i) and !df.ui.economic_stone[i.mat_index] and i.isTemperatureSafe(11640)
                }
                    custom = df.world.raws.buildings.all.find { |b| b.code == 'SOAP_MAKER' }.id
                    bld = df.building_alloc(:Workshop, :Custom, custom)
                    df.building_position(bld, r)
                    df.building_construct(bld, [bucket, bould])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :MetalsmithsForge
                # anvil, boulder
                if anvil = df.world.items.other[:ANVIL].find { |i|
                        i.kind_of?(DFHack::ItemAnvilst) and df.item_isfree(i) and i.isTemperatureSafe(11640)
                } and bould = df.world.items.other[:BOULDER].find { |i|
                        i.kind_of?(DFHack::ItemBoulderst) and df.item_isfree(i) and !df.ui.economic_stone[i.mat_index] and i.isTemperatureSafe(11640)
                }
                    bld = df.building_alloc(:Workshop, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [anvil, bould])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :WoodFurnace, :Smelter
                # firesafe boulder
                if bould = df.world.items.other[:BOULDER].find { |i|
                        i.kind_of?(DFHack::ItemBoulderst) and df.item_isfree(i) and !df.ui.economic_stone[i.mat_index] and i.isTemperatureSafe(11640)
                }
                    bld = df.building_alloc(:Furnace, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [bould])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :Quern
                if quern = df.world.items.other[:QUERN].find { |i|
                        i.kind_of?(DFHack::ItemQuernst) and df.item_isfree(i)
                }
                    bld = df.building_alloc(:Workshop, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [quern])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            else
                # any non-eco boulder
                if bould = df.map_tile_at(r).mapblock.items_tg.find { |i|
                        # check map_block.items first
                        i.kind_of?(DFHack::ItemBoulderst) and df.item_isfree(i) and
                        !df.ui.economic_stone[i.mat_index] and
                        i.pos.x >= r.x1 and i.pos.x <= r.x2 and i.pos.y >= r.y1 and i.pos.y <= r.y2
                } || df.world.items.other[:BOULDER].find { |i|
                        i.kind_of?(DFHack::ItemBoulderst) and df.item_isfree(i) and
                        !df.ui.economic_stone[i.mat_index]
                }
                    bld = df.building_alloc(:Workshop, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [bould])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                # XXX else quarry?
                end
            end
        end

        def construct_stockpile(r)
            bld = df.building_alloc(:Stockpile)
            df.building_position(bld, r)
            bld.room.extents = df.malloc(r.w*r.h)
            bld.room.x = r.x1
            bld.room.y = r.y1
            bld.room.width = r.w
            bld.room.height = r.h
            r.w.times { |x| r.h.times { |y| bld.room.extents[x+r.w*y] = 1 } }
            bld.is_room = 1
            df.building_construct_abstract(bld)
            r.misc[:bld_id] = bld.id
            furnish_room(r)

            setup_stockpile_settings(r, bld)

            room_items(r) { |i| i.flags.dump = true } if r.misc[:workshop] and r.subtype != :stone

            if r.subtype == :refuse and not r.misc[:workshop]
                ensure_stockpile(:corpses)
            end

            if r.misc[:workshop] or r.misc[:secondary]
                ensure_stockpile(r.subtype)
                if main = find_room(:stockpile) { |o| o.subtype == r.subtype and not o.misc[:workshop] and not o.misc[:secondary]} and mb = main.dfbuilding
                    bld, mb = mb, bld if r.misc[:secondary]
                    mb.links.give_to_pile << bld
                    bld.links.take_from_pile << mb
                end
            else
                @rooms.each { |o|
                    if o.type == :stockpile and o.subtype == r.subtype and (o.misc[:workshop] or o.misc[:secondary]) and sub = o.dfbuilding
                        bld, sub = sub, bld if o.misc[:secondary]
                        bld.links.give_to_pile << sub
                        sub.links.take_from_pile << bld
                        bld, sub = sub, bld if o.misc[:secondary]
                    end
                }
            end
        end

        def construct_activityzone(r)
            bld = df.building_alloc(:Civzone, :ActivityZone)
            bld.zone_flags.active = true
            case r.type
            when :infirmary
                bld.zone_flags.hospital = true
                bld.hospital.max_splints = 5
                bld.hospital.max_thread = 75000
                bld.hospital.max_cloth = 50000
                bld.hospital.max_crutches = 5
                bld.hospital.max_plaster = 750
                bld.hospital.max_buckets = 2
                bld.hospital.max_soap = 750
                setup_infirmary_supplies(r)
            when :garbagedump
                bld.zone_flags.garbage_dump = true
            when :pasture
                bld.zone_flags.pen_pasture = true
                # pit_flags |= 2
            end
            df.building_position(bld, r)
            bld.room.extents = df.malloc(r.w*r.h)
            bld.room.x = r.x1
            bld.room.y = r.y1
            bld.room.width = r.w
            bld.room.height = r.h
            r.w.times { |x| r.h.times { |y| bld.room.extents[x+r.w*y] = 1 } }
            bld.is_room = 1
            df.building_construct_abstract(bld)
            r.misc[:bld_id] = bld.id
            furnish_room(r)
        end

        def setup_infirmary_supplies(r)
            return unless df.world.items.other[:BOULDER].find { |i|
                m = df.decode_mat(i) and m.material and m.material.reaction_class.include?('GYPSUM')
            }

            # XXX ugly
            wr = Room.new(nil, nil, r.x1+1, r.x1+3, r.y2+2, r.y2+4, r.z)
            wr.dig
            if bould = df.world.items.other[:BOULDER].find { |i|
                i.kind_of?(DFHack::ItemBoulderst) and df.item_isfree(i) and !df.ui.economic_stone[i.mat_index] and i.isTemperatureSafe(11640)
            }
                bld = df.building_alloc(:Furnace, :Kiln)
                df.building_position(bld, wr)
                df.building_construct(bld, [bould])

                add_manager_order(:MakePlasterPowder, 15)

                df.onupdate_register_once('df-ai plan infirmary plasterpowder', 2400, 8) {
                    if find_manager_orders(:MakePlasterPowder).empty?
                        df.building_deconstruct(bld)
                        true
                    end
                }
            end
        end

        def setup_stockpile_settings(r, bld)
            case r.subtype
            when :stone
                bld.settings.flags.stone = true
                t = bld.settings.stone.mats
                if r.misc[:workshop] and r.misc[:workshop].subtype == :Masons
                    df.world.raws.inorganics.length.times { |i|
                        t[i] = (df.ui.economic_stone[i] ? 0 : 1)
                    }
                elsif r.misc[:workshop] and r.misc[:workshop].subtype == :Smelter
                    df.world.raws.inorganics.length.times { |i|
                        t[i] = (df.world.raws.inorganics[i].flags[:METAL_ORE] ? 1 : 0)
                    }
                else
                    df.world.raws.inorganics.length.times { |i| t[i] = 1 }
                end
                bld.max_wheelbarrows = 1
                add_manager_order(:MakeWoodenWheelbarrow)
            when :wood
                bld.settings.flags.wood = true
                t = bld.settings.wood.mats
                df.world.raws.plants.all.length.times { |i| t[i] = 1 }
            when :furniture, :finished_goods, :ammo, :weapons, :armor
                case r.subtype
                when :furniture
                    bld.settings.flags.furniture = true
                    bld.settings.furniture.sand_bags = true
                    s = bld.settings.furniture
                    60.times { |i| s.type[i] = true }   # 33, hardcoded (28 ItemTypes, 4 ToolUses, 1 SandBags)
                when :finished_goods
                    bld.settings.flags.finished_goods = true
                    s = bld.settings.finished_goods
                    150.times { |i| s.type[i] = true }  # 112 (== refuse)
                    bld.max_bins = r.w*r.h
                when :ammo
                    bld.settings.flags.ammo = true
                    s = bld.settings.ammo
                    df.world.raws.itemdefs.ammo.length.times { |i| s.type[i] = true }
                    bld.max_bins = r.w*r.h
                when :weapons
                    bld.settings.flags.weapons = true
                    s = bld.settings.weapons
                    df.world.raws.itemdefs.weapons.length.times { |i| s.weapon_type[i] = true }
                    df.world.raws.itemdefs.trapcomps.length.times { |i| s.trapcomp_type[i] = true }
                    s.usable = true
                    s.unusable = true
                    bld.max_bins = r.w*r.h
                when :armor
                    bld.settings.flags.armor = true
                    s = bld.settings.armor
                    df.world.raws.itemdefs.armor.length.times { |i| s.body[i] = true }
                    df.world.raws.itemdefs.helms.length.times { |i| s.head[i] = true }
                    df.world.raws.itemdefs.shoes.length.times { |i| s.feet[i] = true }
                    df.world.raws.itemdefs.pants.length.times { |i| s.legs[i] = true }
                    df.world.raws.itemdefs.gloves.length.times { |i| s.hands[i] = true }
                    df.world.raws.itemdefs.shields.length.times { |i| s.shield[i] = true }
                    s.usable = true
                    s.unusable = true
                    bld.max_bins = r.w*r.h
                end
                30.times { |i| s.other_mats[i] = true }    # 10
                df.world.raws.inorganics.length.times { |i| s.mats[i] = true }
                s.quality_core.map! { true }
                s.quality_total.map! { true }
            when :animals
                bld.settings.flags.animals = true
                bld.settings.animals.empty_cages = (r.misc[:secondary] ? true : false)
                bld.settings.animals.empty_traps = (r.misc[:secondary] ? true : false)
                df.world.raws.creatures.all.length.times { |i| bld.settings.animals.enabled[i] = true }
            when :refuse, :corpses
                bld.settings.flags.refuse = true
                bld.settings.refuse.fresh_raw_hide = false
                bld.settings.refuse.rotten_raw_hide = true
                t = bld.settings.refuse
                150.times { |i| t.type[i] = true }  # 112, ItemType enum + other stuff
                if r.misc[:workshop] and r.misc[:workshop].subtype == :Craftsdwarfs
                    df.world.raws.creatures.all.length.times { |i|
                        t.corpses[i] = t.body_parts[i] = t.hair[i] = false
                        t.skulls[i] = t.bones[i] = t.shells[i] = t.teeth[i] = t.horns[i] = true
                    }
                elsif r.subtype == :corpses
                    df.world.raws.creatures.all.length.times { |i|
                        t.corpses[i] = true
                        t.body_parts[i] = t.skulls[i] = t.bones[i] = t.hair[i] =
                            t.shells[i] = t.teeth[i] = t.horns[i] = false
                    }
                else
                    df.world.raws.creatures.all.length.times { |i|
                        t.corpses[i] = false
                        t.body_parts[i] = t.skulls[i] = t.bones[i] = t.hair[i] =
                            t.shells[i] = t.teeth[i] = t.horns[i] = true
                    }
                end
            when :food
                bld.settings.flags.food = true
                bld.max_barrels = r.w*r.h
                t = bld.settings.food
                mt = df.world.raws.mat_table
                   if r.misc[:workshop] and r.misc[:workshop].type == :farmplot
                    mt.organic_types[:Seed].length.times { |i| t.seeds[i] = true }
                elsif r.misc[:workshop] and r.misc[:workshop].subtype == :Farmers
                    mt.organic_types[:Plants].length.times { |i|
                        # include MILL because the quern is near
                        plant = df.decode_mat(mt.organic_types[:Plants][i], mt.organic_indexes[:Plants][i]).plant
                        t.plants[i] = (plant.flags[:THREAD] or plant.flags[:LEAVES] or plant.flags[:MILL]) if plant
                    }
                elsif r.misc[:workshop] and r.misc[:workshop].subtype == :Still
                    mt.organic_types[:Plants].length.times { |i|
                        plant = df.decode_mat(mt.organic_types[:Plants][i], mt.organic_indexes[:Plants][i]).plant
                        t.plants[i]= plant.flags[:DRINK] if plant
                    }
                elsif r.misc[:workshop] and r.misc[:workshop].subtype == :Kitchen
                    mt.organic_types[:Leaf          ].length.times { |i| t.leaves[i]          = true }
                elsif r.misc[:workshop] and r.misc[:workshop].subtype == :Fishery
                    mt.organic_types[:UnpreparedFish].length.times { |i| t.unprepared_fish[i] = true }
                else
                    bld.settings.food.prepared_meals = true
                    mt.organic_types[:Meat          ].length.times { |i| t.meat[i]            = true }    # XXX very big (10588)
                    mt.organic_types[:Fish          ].length.times { |i| t.fish[i]            = true }
                    mt.organic_types[:UnpreparedFish].length.times { |i| t.unprepared_fish[i] = true }
                    mt.organic_types[:Eggs          ].length.times { |i| t.egg[i]             = true }
                    mt.organic_types[:Plants        ].length.times { |i| t.plants[i]          = true }
                    mt.organic_types[:PlantDrink    ].length.times { |i| t.drink_plant[i]     = true }
                    mt.organic_types[:CreatureDrink ].length.times { |i| t.drink_animal[i]    = true }
                    mt.organic_types[:PlantCheese   ].length.times { |i| t.cheese_plant[i]    = true }
                    mt.organic_types[:CreatureCheese].length.times { |i| t.cheese_animal[i]   = true }
                    mt.organic_types[:Seed          ].length.times { |i| t.seeds[i]           = true }
                    mt.organic_types[:Leaf          ].length.times { |i| t.leaves[i]          = true }
                    mt.organic_types[:PlantPowder   ].length.times { |i| t.powder_plant[i]    = true }
                    mt.organic_types[:CreaturePowder].length.times { |i| t.powder_creature[i] = true }
                    mt.organic_types[:Glob          ].length.times { |i| t.glob[i]            = true }
                    mt.organic_types[:Paste         ].length.times { |i| t.glob_paste[i]      = true }
                    mt.organic_types[:Pressed       ].length.times { |i| t.glob_pressed[i]    = true }
                    mt.organic_types[:PlantLiquid   ].length.times { |i| t.liquid_plant[i]    = true }
                    mt.organic_types[:CreatureLiquid].length.times { |i| t.liquid_animal[i]   = true }
                    mt.organic_types[:MiscLiquid    ].length.times { |i| t.liquid_misc[i]     = true }
                end
            when :cloth
                bld.settings.flags.cloth = true
                bld.max_bins = r.w*r.h
                t = bld.settings.cloth
                if r.misc[:workshop] and r.misc[:workshop].subtype == :Loom
                    df.world.raws.mat_table.organic_types[:Silk       ].length.times { |i| t.thread_silk[i]  = true ; t.cloth_silk[i]  = false }
                    df.world.raws.mat_table.organic_types[:PlantFiber ].length.times { |i| t.thread_plant[i] = true ; t.cloth_plant[i] = false }
                    df.world.raws.mat_table.organic_types[:Yarn       ].length.times { |i| t.thread_yarn[i]  = true ; t.cloth_yarn[i]  = false }
                    df.world.raws.mat_table.organic_types[:MetalThread].length.times { |i| t.thread_metal[i] = false; t.cloth_metal[i] = false }
                elsif r.misc[:workshop] and r.misc[:workshop].subtype == :Clothiers
                    df.world.raws.mat_table.organic_types[:Silk       ].length.times { |i| t.thread_silk[i]  = false ; t.cloth_silk[i]  = true }
                    df.world.raws.mat_table.organic_types[:PlantFiber ].length.times { |i| t.thread_plant[i] = false ; t.cloth_plant[i] = true }
                    df.world.raws.mat_table.organic_types[:Yarn       ].length.times { |i| t.thread_yarn[i]  = false ; t.cloth_yarn[i]  = true }
                    df.world.raws.mat_table.organic_types[:MetalThread].length.times { |i| t.thread_metal[i] = false ; t.cloth_metal[i] = true }
                else
                    df.world.raws.mat_table.organic_types[:Silk       ].length.times { |i| t.thread_silk[i]  = t.cloth_silk[i]  = true }
                    df.world.raws.mat_table.organic_types[:PlantFiber ].length.times { |i| t.thread_plant[i] = t.cloth_plant[i] = true }
                    df.world.raws.mat_table.organic_types[:Yarn       ].length.times { |i| t.thread_yarn[i]  = t.cloth_yarn[i]  = true }
                    df.world.raws.mat_table.organic_types[:MetalThread].length.times { |i| t.thread_metal[i] = t.cloth_metal[i] = true }
                end
            when :leather
                bld.settings.flags.leather = true
                bld.max_bins = r.w*r.h
                t = bld.settings.leather.mats
                df.world.raws.mat_table.organic_types[:Leather].length.times { |i| t[i] = true }
            when :gems
                bld.settings.flags.gems = true
                bld.max_bins = r.w*r.h
                t = bld.settings.gems
                if r.misc[:workshop] and r.misc[:workshop].subtype == :Jewelers
                    df.world.raws.mat_table.builtin.length.times { |i| t.rough_other_mats[i] = t.cut_other_mats[i] = false }
                    df.world.raws.inorganics.length.times { |i| t.rough_mats[i] = true ; t.cut_mats[i] = false }
                else
                    df.world.raws.mat_table.builtin.length.times { |i| t.rough_other_mats[i] = t.cut_other_mats[i] = true }
                    df.world.raws.inorganics.length.times { |i| t.rough_mats[i] = t.cut_mats[i] = true }
                end
            when :bars_blocks
                bld.settings.flags.bars_blocks = true
                bld.max_bins = r.w*r.h
                s = bld.settings.bars_blocks
                if r.misc[:workshop] and r.misc[:workshop].subtype == :MetalsmithsForge
                    df.world.raws.inorganics.length.times { |i| s.bars_mats[i] = df.world.raws.inorganics[i].material.flags[:IS_METAL] }
                    s.bars_other_mats[0] = true  # coal
                    bld.settings.allow_organic = false
                else
                    df.world.raws.inorganics.length.times { |i| s.bars_mats[i] = s.blocks_mats[i] = true }
                    10.times { |i| s.bars_other_mats[i] = s.blocks_other_mats[i] = true }
                    # bars_other = 5 (coal/potash/ash/pearlash/soap)     blocks_other = 4 (greenglass/clearglass/crystalglass/wood)
                end
            when :coins
                bld.settings.flags.coins = true
                t = bld.settings.coins.mats
                df.world.raws.inorganics.length.times { |i| t[i] = true }
            end
        end

        def construct_farmplot(r)
            if df.map_tile_at(r).tilemat != :SOIL
                puts "AI: cheat: mud invocation"
                (r.x1..r.x2).each { |x| (r.y1..r.y2).each { |y|
                    t = df.map_tile_at(x, y, r.z)
                    if t.tilemat != :SOIL
                        if not e = t.mapblock.block_events.find { |_e|
                            _e.kind_of?(DFHack::BlockSquareEventMaterialSpatterst) and df.decode_mat(_e).to_s == 'MUD'
                        }
                            mud = df.decode_mat('MUD')
                            e = DFHack::BlockSquareEventMaterialSpatterst.cpp_new(:mat_type => mud.mat_type, :mat_index => mud.mat_index)
                            e.min_temperature = e.max_temperature = 60001
                            t.mapblock.block_events << e
                        end
                        e.amount[t.dx][t.dy] = 50   # small pile of mud
                    end
                } }
            end

            bld = df.building_alloc(:FarmPlot)
            df.building_position(bld, r)
            df.building_construct(bld, [])
            r.misc[:bld_id] = bld.id
            furnish_room(r)
            if st = find_room(:stockpile) { |_r| _r.misc[:workshop] == r }
                digroom(st)
            end
            case r.subtype
            when :food
                ensure_workshop(:Still)
            when :cloth
            end
            @tasks << [:setup_farmplot, r]
        end

        def move_dininghall_fromtemp(r, t)
            # if we dug a real hall, get rid of the temporary one
            t.layout.each { |f|
                if of = r.layout.find { |_f| _f[:item] == f[:item] and _f[:users] == [] }
                    of[:users] = f[:users]
                    of[:queue_build] = true if f[:queue_build]
                    of.delete :ignore unless f[:ignore]
                    if f[:bld_id] and bld = df.building_find(f[:bld_id])
                        df.building_deconstruct(bld)
                    end
                end
            }
            @rooms.delete t
        end

        def smooth_room(r)
            (r.z1..r.z2).each { |z|
                ((r.x1-1)..(r.x2+1)).each { |x|
                    ((r.y1-1)..(r.y2+1)).each { |y|
                        next unless t = df.map_tile_at(x, y, z)
                        next if t.designation.hidden
                        case t.shape_basic
                        when :Wall, :Floor
                            t.dig :Smooth
                        end
                    }
                }
            }
        end

        # smooth a room and its accesspath corridors (recursive)
        def smooth_room_access(r)
            smooth_room(r)
            r.accesspath.each { |a| smooth_room_access(a) }
        end

        def smooth_cistern(r)
            smooth_room_access(r)

            ((r.z1+1)..r.z2).each { |z|
                (r.x1..r.x2).each { |x|
                    (r.y1..r.y2).each { |y|
                        next unless t = df.map_tile_at(x, y, z)
                        t.designation.smooth = 0
                    }
                }
            }

            ai.pop.worker_labor.each { |w, wl|
                df.unit_find(w).status.labors[:DETAIL] = !wl.include?(:MINE)
            }
        end

        def construct_cistern(r)
            # ensure levers are built before floodgates (minimize risk/duration of locked dwarves)
            @rooms.each { |_r| wantdig(_r) if _r.type == :well }
            @rooms.each { |_r| wantdig(_r) if _r.type == :cistern }

            furnish_room(r)
            smooth_cistern(r)

            # remove boulders
            dump_items_access(r)

            # check smoothing progress, channel intermediate levels
            if r.subtype == :well
                @tasks << [:dig_cistern, r]
            end
        end

        # marks all items in room and its access for dumping
        # return true if any item is found
        def dump_items_access(r)
            found = false
            todo = [r]
            while _r = todo.shift
                room_items(_r) { |i| found = i.flags.dump = true }
                todo.concat _r.accesspath
            end
            found
        end

        # yield every on_ground items in the room
        def room_items(r)
            (r.z1..r.z2).each { |z|
                ((r.x1 & -16)..r.x2).step(16) { |x|
                    ((r.y1 & -16)..r.y2).step(16) { |y|
                        df.map_tile_at(x, y, z).mapblock.items_tg.each { |i|
                            yield i if i.flags.on_ground and i.pos.x >= r.x1 and i.pos.x <= r.x2 and i.pos.y >= r.y1 and i.pos.y <= r.y2 and i.pos.z == z
                        }
                    }
                }
            }
        end

        # check smoothing progress, channel intermediate floors when done
        def try_digcistern(r)
            issmooth = lambda { |t|
                next if not t
                next true if t.shape_basic == :Open
                next true if t.tilemat == :SOIL
                next true if t.shape_basic == :Wall and t.caption =~ /pillar|smooth/i
            }

            # XXX hardcoded layout..
            cnt = 0
            acc_y = r.accesspath[0].y1
            ((r.z1+1)..r.z2).each { |z|
                (r.x1..r.x2).each { |x|
                    (r.y1..r.y2).each { |y|
                        next unless t = df.map_tile_at(x, y, z)
                        case t.shape_basic
                        when :Floor
                            next unless issmooth[df.map_tile_at(x+1, y-1, z)]
                            next unless issmooth[df.map_tile_at(x+1, y, z)]
                            next unless issmooth[df.map_tile_at(x+1, y+1, z)]
                            next unless nt01 = df.map_tile_at(x-1, y, z)
                            if nt01.shape_basic == :Floor
                                t.dig :Channel
                            else
                                # last column before stairs
                                next unless nt10 = df.map_tile_at(x, y-1, z)
                                next unless nt12 = df.map_tile_at(x, y+1, z)
                                t.dig :Channel if (y > acc_y ? issmooth[nt12] : issmooth[nt10])
                            end
                        when :Open
                            cnt += 1 if x >= r.x1 and x <= r.x2 and y >= r.y1 and y <= r.y2
                        end
                    }
                }
            }

            @trycistern_count ||= 0
            @trycistern_count += 1
            smooth_cistern(r) if @trycistern_count % 12 == 0

            if cnt == r.w*r.h*(r.h_z-1)
                r.misc[:channeled] = true
                true
            end
        end

        def dig_garbagedump
            @rooms.each { |r|
                if r.type == :garbagepit and r.status == :plan
                    r.status = :dig
                    r.dig(:Channel)
                    @tasks << [:dig_garbage, r]
                end
            }
        end

        def try_diggarbage(r)
            if r.dug?(:Open)
                r.status = :dug
                # XXX ugly as usual
                t = df.map_tile_at(r.x1, r.y1, r.z1-1)
                t.dig(:Default) if t.shape_basic == :Ramp
                @rooms.each { |r|
                    if r.type == :garbagedump and r.status == :plan
                        construct_activityzone(r)
                    end
                }
            else
                # tree ?
                r.dig(:Channel)
                false
            end
        end

        def try_setup_farmplot(r)
            bld = r.dfbuilding
            if not bld
                puts "MOO"
                return true
            end
            return if bld.getBuildStage < bld.getMaxBuildStage

            may = []
            df.world.raws.plants.all.length.times { |i|
                p = df.world.raws.plants.all[i]
                next if not p.flags[:BIOME_SUBTERRANEAN_WATER]
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

                        pm = p.material[0]
                        if isfirst
                            pm.flags[:EDIBLE_RAW] and p.flags[:DRINK]
                        else
                            pm.flags[:EDIBLE_RAW] or pm.flags[:EDIBLE_COOKED] or p.flags[:DRINK] or p.flags[:LEAVES]
                        end
                    }

                    bld.plant_id[season] = pids[rand(pids.length)] unless pids.empty?
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
                    pids |= dyes.find_all { |i|
                        # all plot gets dyes only if no thread candidate exists
                        next if !pids.empty? #and isfirst
                        p = df.world.raws.plants.all[i]
                        p.flags[season]
                    }

                    bld.plant_id[season] = pids[rand(pids.length)] unless pids.empty?
                }

                # TODO repurpose fields if we have too much dimple dye or smth
            end

            true
        end

        def try_endfurnish(r, f)
            bld = df.building_find(f[:bld_id]) if f[:bld_id]
            if not bld
                #puts "AI: destroyed building ? #{r.inspect} #{f.inspect}"
                return true
            end
            return if bld.getBuildStage < bld.getMaxBuildStage

            case f[:item]
            when :coffin
                bld.burial_mode.allow_burial = true
                bld.burial_mode.no_citizens = false
                bld.burial_mode.no_pets = true
            when :door
                bld.door_flags.internal = true if f[:internal]
            when :trap
                return setup_lever(r, f) if f[:subtype] == :lever
            when :floodgate
                @rooms.each { |rr|
                    next if rr.status == :plan
                    rr.layout.each { |ff|
                        link_lever(ff, f) if ff[:item] == :trap and ff[:subtype] == :lever and ff[:target] == f
                    }
                } if f[:way]
            end

            return true unless f[:makeroom]

            debug "makeroom #{@rooms.index(r)} #{r.type} #{r.subtype}"

            df.free(bld.room.extents._getp) if bld.room.extents
            bld.room.extents = df.malloc((r.w+2)*(r.h+2))
            bld.room.x = r.x1-1
            bld.room.y = r.y1-1
            bld.room.width = r.w+2
            bld.room.height = r.h+2
            set_ext = lambda { |x, y, v| bld.room.extents[bld.room.width*(y-bld.room.y)+(x-bld.room.x)] = v }
            (r.x1-1 .. r.x2+1).each { |rx| (r.y1-1 .. r.y2+1).each { |ry|
                if df.map_tile_at(rx, ry, r.z).shape == :WALL
                    set_ext[rx, ry, 2]
                else
                    set_ext[rx, ry, 3]
                end
            } }
            r.layout.each { |d|
                next if d[:item] != :door
                x = r.x1 + d[:x].to_i
                y = r.y1 + d[:y].to_i
                set_ext[x, y, 0]
                # tile in front of the door tile is 4   (TODO door in corner...)
                set_ext[x+1, y, 4] if x < r.x1
                set_ext[x-1, y, 4] if x > r.x2
                set_ext[x, y+1, 4] if y < r.y1
                set_ext[x, y-1, 4] if y > r.y2
            }
            bld.is_room = 1
            df.building_linkrooms(bld)

            set_owner(r, r.owner)
            furnish_room(r)

            case r.type
            when :dininghall
                bld.table_flags.meeting_hall = true

            when :barracks
                # TODO waterskins/backpacks
                if squad_id = r.misc[:squad_id]
                    assign_barrack_squad(r, squad_id)
                end

            end

            true
        end

        def setup_lever(r, f)
            case r.type
            when :well
                way = f[:way]
                f[:target] ||= find_room(:cistern) { |_r| _r.subtype == :reserve }.layout.find { |_f|
                    _f[:item] == :floodgate and _f[:way] == f[:way]
                }

                if link_lever(f, f[:target])
                    pull_lever(f)
                    @tasks << [:monitor_cistern] if way == :in

                    true
                end
            end
        end

        def link_lever(src, dst)
            return if not src[:bld_id] or not dst[:bld_id]
            bld = df.building_find(src[:bld_id])
            return if not bld or bld.getBuildStage < bld.getMaxBuildStage
            tbld = df.building_find(dst[:bld_id])
            return if not tbld or tbld.getBuildStage < tbld.getMaxBuildStage
            return if bld.jobs.find { |j| j.job_type == :LinkBuildingToTrigger and
                j.general_refs.find { |ref| ref.kind_of?(DFHack::GeneralRefBuildingTriggertargetst) and
                    ref.building_id == tbld.id }
            }

            mechas = df.world.items.other[:TRAPPARTS].find_all { |i|
                i.kind_of?(DFHack::ItemTrappartsst) and df.item_isfree(i)
            }[0, 2]
            return if mechas.length < 2

            reflink = DFHack::GeneralRefBuildingTriggertargetst.cpp_new
            reflink.building_id = tbld.id
            refhold = DFHack::GeneralRefBuildingHolderst.cpp_new
            refhold.building_id = bld.id

            job = DFHack::Job.cpp_new
            job.job_type = :LinkBuildingToTrigger
            job.general_refs << reflink << refhold
            bld.jobs << job
            df.job_link job

            df.job_attachitem(job, mechas[0], :LinkToTarget)
            df.job_attachitem(job, mechas[1], :LinkToTrigger)
        end

        def pull_lever(f)
            bld = df.building_find(f[:bld_id])
            return if not bld
            debug "pull lever #{f[:way]}"

            ref = DFHack::GeneralRefBuildingHolderst.cpp_new
            ref.building_id = bld.id

            job = DFHack::Job.cpp_new
            job.job_type = :PullLever
            job.pos = [bld.x1, bld.y1, bld.z]
            job.general_refs << ref
            bld.jobs << job
            df.job_link job
        end

        def monitor_cistern
            if not @m_c_lever_in
                well = find_room(:well)
                @m_c_lever_in = well.layout.find { |f| f[:item] == :trap and f[:subtype] == :lever and f[:way] == :in }
                @m_c_lever_out = well.layout.find { |f| f[:item] == :trap and f[:subtype] == :lever and f[:way] == :out }
                @m_c_cistern = find_room(:cistern) { |r| r.subtype == :well }
                @m_c_reserve = find_room(:cistern) { |r| r.subtype == :reserve }
                @m_c_testgate_delay = 2 if @m_c_reserve.misc[:channel_enable]
            end

            l_in = df.building_find(@m_c_lever_in[:bld_id]) if @m_c_lever_in[:bld_id]
            f_in = df.building_find(@m_c_lever_in[:target][:bld_id]) if @m_c_lever_in[:target] and @m_c_lever_in[:target][:bld_id]
            l_out = df.building_find(@m_c_lever_out[:bld_id]) if @m_c_lever_out[:bld_id]
            f_out = df.building_find(@m_c_lever_out[:target][:bld_id]) if @m_c_lever_out[:target] and @m_c_lever_out[:target][:bld_id]
            return unless l_in and f_in and l_out and f_out
            return unless l_in.linked_mechanisms.first and l_out.linked_mechanisms.first
            return unless l_in.jobs.empty? and l_out.jobs.empty?

            f_in_closed = true if f_in.gate_flags.closed or f_in.gate_flags.closing
            f_in_closed = false if f_in.gate_flags.opening
            f_out_closed = true if f_out.gate_flags.closed or f_out.gate_flags.closing
            f_out_closed = false if f_out.gate_flags.opening

            if @m_c_testgate_delay
                # expensive, dont check too often
                @m_c_testgate_delay -= 1
                return if @m_c_testgate_delay > 0

                well = find_room(:well)
                return if df.map_tile_at(well).offset(0, 0, -1).shape_basic != :Open

                # re-dump garbage + smooth reserve accesspath
                construct_cistern(@m_c_reserve)

                gate = df.map_tile_at(*@m_c_reserve.misc[:channel_enable])
                if gate.shape_basic == :Wall
                    debug 'cistern: test channel'
                    empty = true
                    todo = [@m_c_reserve]
                    while empty and r = todo.shift
                        todo.concat r.accesspath
                        empty = false if (r.x1..r.x2).find { |x| (r.y1..r.y2).find { |y| (r.z1..r.z2).find { |z|
                            t = df.map_tile_at(x, y, z) and (t.designation.smooth == 1 or t.occupancy.unit)
                        } } }
                    end

                    if empty and @m_c_cistern.misc[:channeled] and !dump_items_access(@m_c_cistern) and !dump_items_access(@m_c_reserve)
                        debug 'cistern: do channel'
                        gate.offset(0, 0, 1).dig(:Channel)
                        pull_lever(@m_c_lever_out) if not f_out_closed
                    elsif well.maptile1.offset(-2, well.h/2).designation.flow_size == 7
                        # something went not as planned, but we have a water source
                        @m_c_testgate_delay = nil
                    else
                        @m_c_testgate_delay = 16
                    end
                else
                    @m_c_testgate_delay = nil
                end
                return
            end

            # cistlvl = water level for the next-to-top floor
            cistlvl = df.map_tile_at(@m_c_cistern).designation.flow_size
            resvlvl = df.map_tile_at(@m_c_reserve).designation.flow_size

            if resvlvl <= 1
                # reserve empty, fill it
                if not f_out_closed
                    pull_lever(@m_c_lever_out)
                elsif f_in_closed
                    pull_lever(@m_c_lever_in)
                end
            elsif resvlvl == 7
                # reserve full, empty into cistern if needed
                if not f_in_closed
                    pull_lever(@m_c_lever_in)
                else
                    if cistlvl < 7
                        if f_out_closed
                            pull_lever(@m_c_lever_out)
                        end
                    else
                        if not f_out_closed
                            pull_lever(@m_c_lever_out)
                        end
                    end
                end
            else
                # ensure it's either filling up or emptying
                if f_in_closed and f_out_closed
                    if resvlvl >= 6 and cistlvl < 7
                        pull_lever(@m_c_lever_out)
                    else
                        pull_lever(@m_c_lever_in)
                    end
                end
            end
        end

        def try_endconstruct(r)
            bld = r.dfbuilding
            return if bld and bld.getBuildStage < bld.getMaxBuildStage
            furnish_room(r)
            true
        end

        ManagerRealOrder = {
            :MakeSoap => :CustomReaction,
            :MakePlasterPowder => :CustomReaction,
            :MakeBag => :ConstructChest,
            :MakeRope => :MakeChain,
            :MakeWoodenWheelbarrow => :MakeTool,
            :MakeBoneBolt => :MakeAmmo,
            :MakeBoneCrossbow => :MakeWeapon,
            :MakeTrainingAxe => :MakeWeapon,
            :MakeTrainingShortSword => :MakeWeapon,
            :MakeTrainingSpear => :MakeWeapon,
        }
        ManagerMatCategory = {
            :MakeRope => :cloth, :MakeBag => :cloth,
            :ConstructBed => :wood, :MakeBarrel => :wood, :MakeBucket => :wood, :ConstructBin => :wood,
            :MakeWoodenWheelbarrow => :wood, :MakeTrainingAxe => :wood,
            :MakeTrainingShortSword => :wood, :MakeTrainingSpear => :wood,
            :ConstructCrutch => :wood, :ConstructSplint => :wood, :MakeCage => :wood,
            :MakeBoneBolt => :bone, :MakeBoneCrossbow => :bone,
        }
        ManagerType = {   # no MatCategory => mat_type = 0 (ie generic rock), unless specified here
            :ProcessPlants => -1, :ProcessPlantsBag => -1, :MillPlants => -1, :BrewDrink => -1,
            :ConstructTractionBench => -1, :MakeSoap => -1, :MakeLye => -1, :MakeAsh => -1,
            :MakeTotem => -1, :MakeCharcoal => -1, :MakePlasterPowder => -1, :PrepareMeal => 4,
            :DyeCloth => -1,
        }
        ManagerCustom = {
            :MakeSoap => 'MAKE_SOAP_FROM_TALLOW',
            :MakePlasterPowder => 'MAKE_PLASTER_POWDER',
        }
        ManagerSubtype = {
            # depends on raws.itemdefs, wait until a world is loaded
        }

        def self.init_manager_subtype
            ManagerSubtype.update \
                :MakeWoodenWheelbarrow => df.world.raws.itemdefs.tools.find { |d| d.id == 'ITEM_TOOL_WHEELBARROW' }.subtype,
                :MakeTrainingAxe => df.world.raws.itemdefs.weapons.find { |d| d.id == 'ITEM_WEAPON_AXE_TRAINING' }.subtype,
                :MakeTrainingShortSword => df.world.raws.itemdefs.weapons.find { |d| d.id == 'ITEM_WEAPON_SWORD_SHORT_TRAINING' }.subtype,
                :MakeTrainingSpear => df.world.raws.itemdefs.weapons.find { |d| d.id == 'ITEM_WEAPON_SPEAR_TRAINING' }.subtype,
                :MakeBoneBolt => df.world.raws.itemdefs.ammo.find { |d| d.id == 'ITEM_AMMO_BOLTS' }.subtype,
                :MakeBoneCrossbow => df.world.raws.itemdefs.weapons.find { |d| d.id == 'ITEM_WEAPON_CROSSBOW' }.subtype
        end

        if df.world.raws.itemdefs.ammo.empty?
            df.onstatechange_register_once { |st|
                if st == :WORLD_LOADED
                    init_manager_subtype
                    true
                end
            }
        else
            init_manager_subtype
        end


        def find_manager_orders(order)
            _order = ManagerRealOrder[order] || order
            matcat = ManagerMatCategory[order]
            type = ManagerType[order]
            subtype = ManagerSubtype[order]
            custom = ManagerCustom[order]

            df.world.manager_orders.find_all { |_o|
                _o.job_type == _order and
                _o.mat_type == (type || (matcat ? -1 : 0)) and
                (matcat ? _o.material_category.send(matcat) : _o.material_category._whole == 0) and
                (not subtype or subtype == _o.item_subtype) and
                (not custom or custom == _o.reaction_name)
            }
        end

        def add_manager_order(order, amount=1, maxmerge=30)
            debug "add_manager #{order} #{amount}"
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
                    df.world.manager_orders << o
                end
            else
                o.amount_total += amount
                o.amount_left += amount
            end

            case order
            when :ProcessPlants
                ensure_workshop(:Farmers)
            when :ConstructTractionBench
                add_manager_order(:ConstructTable, amount, maxmerge)
                add_manager_order(:MakeRope, amount, maxmerge)
            when :BrewDrink
                ensure_workshop(:Still)
            when :MakeSoap
                add_manager_order(:MakeLye, amount+1, maxmerge)
            when :MakeLye
                add_manager_order(:MakeAsh, amount+1, maxmerge)
            end
        end

        # returns one tile of an outdoor river (if one exists)
        def scan_river
            ifeat = df.world.cur_savegame.map_features.find { |f| f.kind_of?(DFHack::FeatureInitOutdoorRiverst) }
            return if not ifeat
            feat = ifeat.feature

            feat.embark_pos.x.length.times { |i|
                x = 48*(feat.embark_pos.x[i] - df.world.map.region_x)
                y = 48*(feat.embark_pos.y[i] - df.world.map.region_y)
                next if x < 0 or x >= df.world.map.x_count or y < 0 or y >= df.world.map.y_count
                z1, z2 = feat.min_map_z[i], feat.max_map_z[i]
                zhalf = (z1+z2)/2 - 1   # the river is most probably here, try it first
                [zhalf, *(z1..z2)].each { |z|
                    48.times { |dx|
                        48.times { |dy|
                            t = df.map_tile_at(x+dx, y+dy, z)
                            return t if t and t.designation.feature_local
                        }
                    }
                }
            }

            nil
        end

        attr_accessor :fort_entrance, :rooms, :corridors
        def setup_blueprint
            # TODO use existing fort facilities (so we can relay the user or continue from a save)
            # TODO connect all parts of the map (eg river splitting north/south)
            puts 'AI: setting up fort blueprint...'
            # TODO place fort body first, have main stair stop before surface, and place trade depot on path to surface
            scan_fort_entrance
            debug 'blueprint found entrance'
            scan_fort_body
            debug 'blueprint found body'
            setup_blueprint_rooms
            debug 'blueprint found rooms'
            # ensure traps are on the surface
            @fort_entrance.layout.each { |i|
                i[:z] = surface_tile_at(@fort_entrance.x1+i[:x], @fort_entrance.y1+i[:y]).z-@fort_entrance.z1
            }
            puts 'AI: ready'
        end

        # same as tile.spiral_search, but search starting with center of each side first
        def spiral_search(tile, max=100, min=0, step=1)
            (min..max).step(step) { |rng|
                if rng != 0
                    [[rng, 0], [0, rng], [-rng, 0], [0, -rng]].each { |dx, dy|
                        next if not tt = tile.offset(dx, dy)
                        return tt if yield(tt)
                    }
                end
                if tt = tile.spiral_search(rng, rng, step) { |_tt| yield _tt }
                    return tt
                end
            }
            nil
        end

        # search a valid tile for fortress entrance
        def scan_fort_entrance
            # map center
            cx = df.world.map.x_count / 2
            cy = df.world.map.y_count / 2
            center = surface_tile_at(cx, cy)
            rangez = (0...df.world.map.z_count).to_a.reverse
            cz = rangez.find { |z| t = df.map_tile_at(cx, cy, z) and tsb = t.shape_basic and (tsb == :Floor or tsb == :Ramp) }
            center = df.map_tile_at(cx, cy, cz)

            ent0 = center.spiral_search { |t0|
                # test the whole map for 3x5 clear spots
                next unless t = surface_tile_at(t0)
                (-1..1).all? { |_x|
                    (-2..2).all? { |_y|
                        tt = t.offset(_x, _y, -1) and tt.shape == :WALL and tm = tt.tilemat and (tm == :STONE or tm == :MINERAL or tm == :SOIL) and
                        ttt = t.offset(_x, _y) and ttt.shape == :FLOOR and ttt.designation.flow_size == 0 and
                         not ttt.designation.hidden and not df.building_find(ttt)
                    }
                }
            }

            if not ent0
                puts 'AI: cant find fortress entrance spot'
                ent = center
            else
                ent = surface_tile_at(ent0)
            end

            @fort_entrance = Corridor.new(ent.x, ent.x, ent.y-1, ent.y+1, ent.z, ent.z)
            3.times { |i|
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => i-1, :y => -1}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 1,   :y => i}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 1-i, :y => 3}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => -1,  :y => 2-i}
            }
            5.times { |i|
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => i-2, :y => -2, :ignore => true}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 2,   :y => i-1, :ignore => true}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 2-i, :y => 4, :ignore => true}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => -2,  :y => 3-i, :ignore => true}
            }

        end

        # search how much we need to dig to find a spot for the full fortress body
        # here we cheat and work as if the map was fully reveal()ed
        def scan_fort_body
            # use a hardcoded fort layout
            cx, cy, cz = @fort_entrance.x, @fort_entrance.y, @fort_entrance.z
            sz_x, sz_y, sz_z = 35, 22, 5
            @fort_entrance.z1 = (0..cz).to_a.reverse.find { |cz1|
                (-sz_z..1).all? { |dz|
                    # scan perimeter first to quickly eliminate caverns / bad rock layers
                    (-sz_x..sz_x).all? { |dx|
                        [-sz_y, sz_y].all? { |dy|
                            t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                            not t.designation.water_table and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or (dz > -1 and tm == :SOIL))
                        }
                    } and
                    [-sz_x, sz_x].all? { |dx|
                        (-sz_y..sz_y).all? { |dy|
                            t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                            not t.designation.water_table and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or (dz > -1 and tm == :SOIL))
                        }
                    }
                }  and
                # perimeter ok, full scan
                (-sz_z..1).all? { |dz|
                    (-(sz_x-1)..(sz_x-1)).all? { |dx|
                        (-(sz_y-1)..(sz_y-1)).all? { |dy|
                            t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                            not t.designation.water_table and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or (dz > -1 and tm == :SOIL))
                        }
                    }
                }
            }

            raise 'Too many caverns, cant find room for fort. We need more minerals !' if not @fort_entrance.z1
        end

        # assign rooms in the space found by scan_fort_*
        def setup_blueprint_rooms
            # hardcoded layout
            @corridors << @fort_entrance

            fx = @fort_entrance.x
            fy = @fort_entrance.y

            fz = @fort_entrance.z1
            setup_blueprint_workshops(fx, fy, fz, [@fort_entrance])
            
            fz = @fort_entrance.z1 -= 1
            setup_blueprint_utilities(fx, fy, fz, [@fort_entrance])
            
            fz = @fort_entrance.z1 -= 1
            setup_blueprint_stockpiles(fx, fy, fz, [@fort_entrance])
            
            2.times {
                fz = @fort_entrance.z1 -= 1
                setup_blueprint_bedrooms(fx, fy, fz, [@fort_entrance])
            }
        end

        def setup_blueprint_workshops(fx, fy, fz, entr)
            corridor_center0 = Corridor.new(fx-1, fx-1, fy-1, fy+1, fz, fz) 
            corridor_center0.layout << {:item => :door, :x => -1, :y => 0}
            corridor_center0.layout << {:item => :door, :x => -1, :y => 2}
            corridor_center0.accesspath = entr
            @corridors << corridor_center0
            corridor_center2 = Corridor.new(fx+1, fx+1, fy-1, fy+1, fz, fz) 
            corridor_center2.layout << {:item => :door, :x => 1, :y => 0}
            corridor_center2.layout << {:item => :door, :x => 1, :y => 2}
            corridor_center2.accesspath = entr
            @corridors << corridor_center2

            # Millstone, Siege, Custom/screwpress
            # GlassFurnace, Kiln, magma workshops/furnaces, other nobles offices
            types = [:Still,:Kitchen, :Fishery,:Butchers, :Leatherworks,:Tanners,
                :Loom,:Clothiers, :Dyers,:Bowyers, nil,:BookkeepersOffice]
            types += [:Masons,:Carpenters, :Mechanics,:Farmers, :Craftsdwarfs,:Jewelers,
                :Smelter,:MetalsmithsForge, :Ashery,:WoodFurnace, :SoapMaker,:ManagersOffice]

            [-1, 1].each { |dirx|
                prev_corx = (dirx < 0 ? corridor_center0 : corridor_center2)
                ocx = fx + dirx*3
                (1..6).each { |dx|
                    # segments of the big central horizontal corridor
                    cx = fx + dirx*4*dx
                    if dx <= 5
                        cor_x = Corridor.new(ocx, cx, fy-1, fy+1, fz, fz)
                        cor_x.accesspath = [prev_corx]
                        @corridors << cor_x
                    else
                        # last 2 workshops of the row (offices etc) get only a narrow/direct corridor
                        cor_x = Corridor.new(fx+dirx*3, cx, fy, fy, fz, fz)
                        cor_x.accesspath = [(dirx < 0 ? corridor_center0 : corridor_center2)]
                        @corridors << cor_x
                        cor_x = Corridor.new(cx-dirx*3, cx+dirx, fy-1, fy+1, fz, fz)
                        cor_x.accesspath = [@corridors.last]
                        @corridors << cor_x
                    end
                    prev_corx = cor_x
                    ocx = cx+dirx

                    if dirx == 1 and dx == 3
                        # stuff a quern near the farmers'
                        @rooms << Room.new(:workshop, :Quern, cx-2, cx-2, fy+1, fy+1, fz)
                        @rooms.last.accesspath = [cor_x]
                    end

                    @rooms << Room.new(:workshop, types.shift, cx-1, cx+1, fy-5, fy-3, fz)
                    @rooms << Room.new(:workshop, types.shift, cx-1, cx+1, fy+3, fy+5, fz)
                    @rooms[-2, 2].each { |r|
                        r.accesspath = [cor_x]
                        r.layout << {:item => :door, :x => 1, :y => 1-2*(r.y<=>fy)}
                    }
                }
            }

            @rooms.each { |r|
                next if r.type != :workshop
                case r.subtype
                when :ManagersOffice, :BookkeepersOffice
                    r.layout << {:item => :chair, :x => 1, :y => 1, :makeroom => true}
                end
            }
        end

        def setup_blueprint_stockpiles(fx, fy, fz, entr)
            corridor_center0 = Corridor.new(fx-1, fx-1, fy-1, fy+1, fz, fz) 
            corridor_center0.layout << {:item => :door, :x => -1, :y => 0}
            corridor_center0.layout << {:item => :door, :x => -1, :y => 2}
            corridor_center0.accesspath = entr
            @corridors << corridor_center0
            corridor_center2 = Corridor.new(fx+1, fx+1, fy-1, fy+1, fz, fz) 
            corridor_center2.layout << {:item => :door, :x => 1, :y => 0}
            corridor_center2.layout << {:item => :door, :x => 1, :y => 2}
            corridor_center2.accesspath = entr
            @corridors << corridor_center2

            types = [:food,:furniture, :wood,:stone, :refuse, :corpses, :gems,:animals]
            types += [:finished_goods,:bars_blocks, :cloth,:leather, :ammo,:armor, :weapons,:coins]

            # TODO side stairs to workshop level ?
            tmp = []
            [-1, 1].each { |dirx|
                prev_corx = (dirx < 0 ? corridor_center0 : corridor_center2)
                ocx = fx + dirx*3
                (1..4).each { |dx|
                    # segments of the big central horizontal corridor
                    cx = fx + dirx*(8*dx-4)
                    cor_x = Corridor.new(ocx, cx+dirx, fy-1, fy+1, fz, fz)
                    cor_x.accesspath = [prev_corx]
                    @corridors << cor_x
                    prev_corx = cor_x
                    ocx = cx+2*dirx

                    t0, t1 = types.shift, types.shift
                    @rooms << Room.new(:stockpile, t0, cx-3, cx+3, fy-3, fy-4, fz)
                    @rooms << Room.new(:stockpile, t1, cx-3, cx+3, fy+3, fy+4, fz)
                    @rooms[-2, 2].each { |r|
                        r.accesspath = [cor_x]
                        r.layout << {:item => :door, :x => 2, :y => (r.y < fy ? 2 : -1)}
                        r.layout << {:item => :door, :x => 4, :y => (r.y < fy ? 2 : -1)}
                    }
                    t0 = :furniture if t0 == :coins
                    t1 = :furniture if t1 == :coins
                    @rooms << Room.new(:stockpile, t0, cx-3, cx+3, fy-5, fy-11, fz)
                    @rooms << Room.new(:stockpile, t1, cx-3, cx+3, fy+5, fy+11, fz)
                    @rooms[-2, 2].each { |r| r.misc[:secondary] = true }
                    tmp << Room.new(:stockpile, t0, cx-3, cx+3, fy-12, fy-20, fz)
                    tmp << Room.new(:stockpile, t1, cx-3, cx+3, fy+12, fy+20, fz)
                    tmp[-2, 2].each { |r| r.misc[:secondary] = true }
                }
            }
            # tweak the order of 2nd secondary stockpiles in @rooms (they are dug in order)
            @rooms.concat tmp
        end

        def setup_blueprint_utilities(fx, fy, fz, entr)
            corridor_center0 = Corridor.new(fx-1, fx-1, fy-1, fy+1, fz, fz) 
            corridor_center0.layout << {:item => :door, :x => -1, :y => 0}
            corridor_center0.layout << {:item => :door, :x => -1, :y => 2}
            corridor_center0.accesspath = entr
            @corridors << corridor_center0
            corridor_center2 = Corridor.new(fx+1, fx+1, fy-1, fy+1, fz, fz) 
            corridor_center2.layout << {:item => :door, :x => 1, :y => 0}
            corridor_center2.layout << {:item => :door, :x => 1, :y => 2}
            corridor_center2.accesspath = entr
            @corridors << corridor_center2

            # dining halls
            ocx = fx-2
            old_cor = corridor_center0

            # temporary dininghall, for fort start
            tmp = Room.new(:dininghall, nil, fx-4, fx-3, fy-1, fy+1, fz)
            tmp.misc[:temporary] = true
            [0, 1, 2].each { |dy|
                tmp.layout << {:item => :table, :x => 0, :y => dy, :users => []}
                tmp.layout << {:item => :chair, :x => 1, :y => dy, :users => []}
            }
            tmp.layout[0][:makeroom] = true
            tmp.accesspath = [old_cor]
            @rooms << tmp


            # dininghalls x 4 (54 users each)
            [0, 1].each { |ax|
                cor = Corridor.new(ocx-1, fx-ax*12-5, fy-1, fy+1, fz, fz)
                cor.accesspath = [old_cor]
                @corridors << cor
                ocx = fx-ax*12-4
                old_cor = cor

                [-1, 1].each { |dy|
                    dinner = Room.new(:dininghall, nil, fx-ax*12-2-10, fx-ax*12-2, fy+dy*9, fy+dy*3, fz)
                    dinner.layout << {:item => :door, :x => 7, :y => (dy>0 ? -1 : 7)}
                    dinner.layout << {:item => :pillar, :x => 2, :y => 3}
                    dinner.layout << {:item => :pillar, :x => 8, :y => 3}
                    [5,4,6,3,7,2,8,1,9].each { |dx|
                        [-1, 1].each { |sy|
                            dinner.layout << {:item => :table, :x => dx, :y => 3+dy*sy*1, :ignore => true, :users => []}
                            dinner.layout << {:item => :chair, :x => dx, :y => 3+dy*sy*2, :ignore => true, :users => []}
                        }
                    }
                    dinner.layout.find { |f| f[:item] == :table }[:makeroom] = true
                    dinner.accesspath = [cor]
                    @rooms << dinner
                }
            }


            if river = scan_river
                setup_blueprint_cistern_fromsource(river, fx, fy, fz)
            else
                # TODO pool, pumps, etc
                puts "AI: no river, no well"
            end


            # farm plots
            farm_h = 3
            farm_w = 3
            dpf = (@dwarves_per_farmtile * farm_h * farm_w).to_i
            nrfarms = (200+dpf-1)/dpf

            cx = fx+4*6       # end of workshop corridor (last ws door)
            cy = fy
            cz = find_room(:workshop).z1
            ws_cor = @corridors.find { |_c| _c.x1 < cx and _c.x2 >= cx and _c.y1 <= cy and _c.y2 >= cy and _c.z1 <= cz and _c.z2 >= cz }
            raise "cannot connect farm to workshop corridor" if not ws_cor
            moo = Corridor.new(cx+1, cx+1, cy, cy, cz, cz)
            moo.accesspath = [ws_cor]
            @corridors << moo
            farm_stairs = Corridor.new(cx+2, cx+2, cy, cy, cz, cz)
            farm_stairs.accesspath = [moo]
            @corridors << farm_stairs
            cx += 3
            soilcnt = {}
            (cz...df.world.map.z_count).each { |z|
                scnt = 0
                if (-1..(nrfarms*farm_w/2)).all? { |dx|
                    (-(2*farm_h+2)..(2*farm_h+2)).all? { |dy|
                        t = df.map_tile_at(cx+dx, cy+dy, z)
                        next if not t or t.shape != :WALL
                        scnt += 1 if t.tilemat == :SOIL
                        true
                    }
                }
                    soilcnt[z] = scnt
                end
            }
            cz2 = soilcnt.index(soilcnt.values.max)

            farm_stairs.z2 = cz2
            cor = Corridor.new(cx, cx+1, cy, cy, cz2, cz2)
            cor.accesspath = [farm_stairs]
            @corridors << cor
            types = [:food, :cloth]
            [-1, 1].each { |dy|
                st = types.shift
                (nrfarms/2).times { |dx|
                    2.times { |ddy|
                        r = Room.new(:farmplot, st, cx+farm_w*dx, cx+farm_w*dx+farm_w-1, cy+dy*2+dy*ddy*farm_h, cy+dy*(2+farm_h-1)+dy*ddy*farm_h, cz2)
                        r.misc[:users] = []
                        if dx == 0 and ddy == 0
                            r.layout << {:item => :door, :x => 1, :y => (dy>0 ? -1 : farm_h)}
                            r.accesspath = [cor]
                        else
                            r.accesspath = [@rooms.last]
                        end
                        @rooms << r
                    }
                }
            }
            # seeds stockpile
            r = Room.new(:stockpile, :food, cx+2, cx+4, cy, cy, cz2)
            r.misc[:workshop] = @rooms[-2*nrfarms]
            r.accesspath = [cor]
            @rooms << r

            # garbage dump
            # TODO ensure flat space, no pools/tree, ...
            r = Room.new(:garbagedump, nil, cx+5, cx+5, cy, cy, cz)
            r.z2 = r.z1 += 1 until df.map_tile_at(r).shape_basic != :Wall
            @rooms << r
            r = Room.new(:garbagepit, nil, cx+6, cx+7, cy, cy, @rooms.last.z)
            @rooms << r

            # infirmary
            old_cor = corridor_center2
            cor = Corridor.new(fx+3, fx+5, fy-1, fy+1, fz, fz)
            cor.accesspath = [old_cor]
            @corridors << cor
            old_cor = cor

            infirmary = Room.new(:infirmary, nil, fx+2, fx+6, fy-3, fy-7, fz)
            infirmary.layout << {:item => :door, :x => 3, :y => 5}
            infirmary.layout << {:item => :bed, :x => 0, :y => 1}
            infirmary.layout << {:item => :table, :x => 1, :y => 1}
            infirmary.layout << {:item => :bed, :x => 2, :y => 1}
            infirmary.layout << {:item => :traction_bench, :x => 0, :y => 2}
            infirmary.layout << {:item => :traction_bench, :x => 2, :y => 2}
            infirmary.layout << {:item => :bed, :x => 0, :y => 3}
            infirmary.layout << {:item => :table, :x => 1, :y => 3}
            infirmary.layout << {:item => :bed, :x => 2, :y => 3}
            infirmary.layout << {:item => :chest, :x => 4, :y => 1}
            infirmary.layout << {:item => :chest, :x => 4, :y => 2}
            infirmary.layout << {:item => :chest, :x => 4, :y => 3}
            infirmary.accesspath = [cor]
            @rooms << infirmary

            # cemetary lots (160 spots)
            cor = Corridor.new(fx+6, fx+14, fy-1, fy+1, fz, fz)
            cor.accesspath = [old_cor]
            @corridors << cor
            old_cor = cor

            2.times { |rrx|
                5.times { |ry|
                    2.times { |rx|
                        ox = fx+10+5*rx + 9*rrx
                        oy = fy-3-3*ry
                        cemetary = Room.new(:cemetary, nil, ox, ox+4, oy, oy-3, fz)
                        4.times { |dx|
                            2.times { |dy|
                                cemetary.layout << {:item => :coffin, :x => dx+1-rx, :y =>dy+1, :ignore => true, :users => []}
                            }
                        }
                        if rx == 0 and ry == 0 and rrx == 0
                            cemetary.layout << {:item => :door, :x => 4, :y => 4}
                            cemetary.accesspath = [cor]
                        end
                        @rooms << cemetary
                    }
                }
            }

            # barracks
            # 8 dwarf per squad, 20% pop => 40 soldiers for 200 dwarves => 5 barracks
            old_cor = corridor_center2
            oldcx = old_cor.x2+2     # door
            4.times { |rx|
                cor = Corridor.new(oldcx, fx+5+10*rx, fy-1, fy+1, fz, fz)
                cor.accesspath = [old_cor]
                @corridors << cor
                old_cor = cor
                oldcx = cor.x2+1

                [1, -1].each { |ry|
                    next if ry == -1 and rx < 3 # infirmary/cemetary

                    barracks = Room.new(:barracks, nil, fx+2+10*rx, fx+2+10*rx+6, fy+3*ry, fy+10*ry, fz)
                    barracks.layout << {:item => :door, :x => 3, :y => (ry > 0 ? -1 : 8)}
                    8.times { |dy|
                        dy = 7-dy if ry < 0
                        barracks.layout << {:item => :armorstand, :x => 5, :y => dy, :ignore => true, :users => []}
                        barracks.layout << {:item => :bed, :x => 6, :y => dy, :ignore => true, :users => []}
                        barracks.layout << {:item => :cabinet, :x => 0, :y => dy, :ignore => true, :users => []}
                        barracks.layout << {:item => :chest, :x => 1, :y => dy, :ignore => true, :users => []}
                    }
                    barracks.layout << {:item => :weaponrack, :x => 4, :y => (ry>0 ? 7 : 0), :makeroom => true, :users => []}
                    barracks.layout << {:item => :weaponrack, :x => 2, :y => (ry>0 ? 7 : 0), :ignore => true, :users => []}
                    barracks.accesspath = [cor]
                    @rooms << barracks
                }
            }

            setup_blueprint_pastures
        end

        def setup_blueprint_cistern_fromsource(src, fx, fy, fz)
            # TODO dynamic layout, at least move the well/cistern on the side of the river
            # TODO scan for solid ground for all this

            # well
            cor = Corridor.new(fx-18, fx-26, fy-1, fy+1, fz, fz)
            cor.accesspath = [@corridors.last]
            @corridors << cor
            
            cx = fx-32
            well = Room.new(:well, :well, cx-4, cx+4, fy-4, fy+4, fz)
            well.layout << {:item => :well, :x => 4, :y => 4, :makeroom => true}
            well.layout << {:item => :door, :x => 9, :y => 3}
            well.layout << {:item => :door, :x => 9, :y => 5}
            well.layout << {:item => :trap, :subtype => :lever, :x => 1, :y => 0, :way => :out}
            well.layout << {:item => :trap, :subtype => :lever, :x => 0, :y => 0, :way => :in}
            well.accesspath = [cor]
            @rooms << well

            # water cistern under the well (in the hole of bedroom blueprint)
            cist_cors = find_corridor_tosurface(df.map_tile_at(cx-8, fy, fz))
            cist_cors[0].z1 -= 3

            cistern = Room.new(:cistern, :well, cx-7, cx+1, fy-1, fy+1, fz-1)
            cistern.z1 -= 2
            cistern.accesspath = [cist_cors[0]]
            @rooms << cistern

            # handle low rivers / high mountain forts
            fz = src.z if fz > src.z
            # should be fine with cistern auto-fill checks
            cistern.z1 = fz if cistern.z1 > fz

            # staging reservoir to fill the cistern, one z-level at a time
            # should have capacity = 1 cistern level @7/7 + itself @1/7 (rounded up)
            #  cistern is 9x3 + 1 (stairs)
            #  reserve is 5x7 (can fill cistern 7/7 + itself 1/7 + 14 spare
            reserve = Room.new(:cistern, :reserve, cx-10, cx-14, fy-3, fy+3, fz)
            reserve.layout << {:item => :floodgate, :x => -1, :y => 3, :way => :in}
            reserve.layout << {:item => :floodgate, :x =>  5, :y => 3, :way => :out}
            reserve.accesspath = [cist_cors[0]]
            @rooms << reserve


            # link the cistern reserve to the water source

            # trivial walk of the river tiles to find a spot closer to dst
            move_river = lambda { |dst|
                nsrc = src
                while nsrc
                    src = nsrc
                    dist = src.distance_to(dst)
                    nsrc = spiral_search(src, 1, 1) { |t|
                        next if t.distance_to(dst) > dist
                        t.designation.feature_local
                    }
                end
            }

            # 1st end: reservoir input
            p1 = df.map_tile_at(cx-16, fy, fz)
            move_river[p1]
            debug "cistern: reserve/in (#{p1.x}, #{p1.y}, #{p1.z}), river (#{src.x}, #{src.y}, #{src.z})"

            # XXX hardcoded layout again
            if src.x <= p1.x+16
                p = p1
                r = reserve
            else
                # the tunnel should walk around other blueprint rooms
                p2 = p1.offset(0, (src.y >= p1.y ? 26 : -26))
                cor = Corridor.new(p1.x, p2.x, p1.y, p2.y, p1.z, p2.z)
                @corridors << cor
                reserve.accesspath << cor
                move_river[p2]
                p = p2
                r = cor
            end

            up = find_corridor_tosurface(p)
            r.accesspath << up[0]

            dst = up[-1].maptile2.offset(0, 0, -1)
            dst = df.map_tile_at(dst.x, dst.y, src.z) if src.z < dst.z
            move_river[dst]

            if (dst.x - src.x).abs > 1
                p3 = dst.offset(src.x-dst.x, 0)
                move_river[p3]
            end

            # find safe tile near the river
            out = spiral_search(src) { |t| map_tile_in_rock(t) }

            # find tile to channel to start water flow
            channel = spiral_search(out, 1, 1) { |t|
                t.spiral_search(1, 1) { |tt| tt.designation.feature_local }
            } || spiral_search(out, 1, 1) { |t|
                t.spiral_search(1, 1) { |tt| tt.designation.flow_size != 0 or tt.tilemat == :FROZEN_LIQUID }
            }
            debug "cistern: channel_enable (#{channel.x}, #{channel.y}, #{channel.z})" if channel

            # TODO check that 'channel' is easily channelable (eg river in a hole)

            if (dst.x - out.x).abs > 1
                cor = Corridor.new(dst.x+(out.x<=>dst.x), out.x, dst.y, dst.y, dst.z, dst.z)
                @corridors << cor
                r.accesspath << cor
                r = cor
            end

            if (dst.y - out.y).abs > 1
                cor = Corridor.new(out.x, out.x, dst.y+(out.y<=>dst.y), out.y, dst.z, dst.z)
                @corridors << cor
                r.accesspath << cor
            end

            reserve.misc[:channel_enable] = [channel.x, channel.y, channel.z] if channel
        end

        # scan for 11x11 flat areas with grass
        def setup_blueprint_pastures
            want = 36
            @fort_entrance.maptile.spiral_search(df.world.map.x_count, 12, 12) { |_t|
                next unless sf = surface_tile_at(_t)
                grasstile = 0
                if (-5..5).all? { |dx| (-5..5).all? { |dy|
                    if tt = sf.offset(dx, dy) and tt.shape_basic == :Floor
                        grasstile += 1 if tt.mapblock.block_events.find { |be|
                            be.kind_of?(DFHack::BlockSquareEventGrassst) and be.amount[tt.dx][tt.dy] > 0
                        }
                        true
                    end
                } } and grasstile >= 70
                    @rooms << Room.new(:pasture, nil, sf.x-5, sf.x+5, sf.y-5, sf.y+5, sf.z)
                    @rooms.last.misc[:users] = []
                    want -= 1
                    true if want == 0
                end
            }
        end

        def setup_blueprint_bedrooms(fx, fy, fz, entr)
            corridor_center0 = Corridor.new(fx-1, fx-1, fy-1, fy+1, fz, fz) 
            corridor_center0.layout << {:item => :door, :x => -1, :y => 0}
            corridor_center0.layout << {:item => :door, :x => -1, :y => 2}
            corridor_center0.accesspath = entr
            @corridors << corridor_center0
            corridor_center2 = Corridor.new(fx+1, fx+1, fy-1, fy+1, fz, fz) 
            corridor_center2.layout << {:item => :door, :x => 1, :y => 0}
            corridor_center2.layout << {:item => :door, :x => 1, :y => 2}
            corridor_center2.accesspath = entr
            @corridors << corridor_center2

            [-1, 1].each { |dirx|
                prev_corx = (dirx < 0 ? corridor_center0 : corridor_center2)
                ocx = fx + dirx*3
                (1..3).each { |dx|
                    # segments of the big central horizontal corridor
                    cx = fx + dirx*(9*dx-4)
                    cor_x = Corridor.new(ocx, cx, fy-1, fy+1, fz, fz)
                    cor_x.accesspath = [prev_corx]
                    @corridors << cor_x
                    prev_corx = cor_x
                    ocx = cx+dirx

                    [-1, 1].each { |diry|
                        prev_cory = cor_x
                        ocy = fy + diry*2
                        (1..6).each { |dy|
                            cy = fy + diry*3*dy
                            cor_y = Corridor.new(cx, cx-dirx*1, ocy, cy, fz, fz)
                            cor_y.accesspath = [prev_cory]
                            @corridors << cor_y
                            prev_cory = cor_y
                            ocy = cy+diry

                            @rooms << Room.new(:bedroom, nil, cx-dirx*4, cx-dirx*3, cy, cy+diry, fz)
                            @rooms << Room.new(:bedroom, nil, cx+dirx*2, cx+dirx*3, cy, cy+diry, fz)
                            @rooms[-2, 2].each { |r|
                                r.accesspath = [cor_y]
                                r.layout << {:item => :bed, :x => (r.x<cx ? 0 : 1), :y => (diry<0 ? 1 : 0), :makeroom => true}
                                r.layout << {:item => :cabinet, :x => (r.x<cx ? 0 : 1), :y => (diry<0 ? 0 : 1), :ignore => true}
                                r.layout << {:item => :chest, :x => (r.x<cx ? 1 : 0), :y => (diry<0 ? 0 : 1), :ignore => true}
                                r.layout << {:item => :door, :x => (r.x<cx ? 2 : -1), :y => (diry<0 ? 1 : 0)}
                            }
                        }
                    }
                }

                # noble suites
                cx = fx + dirx*(9*3-4+6)
                cor_x = Corridor.new(ocx, cx, fy-1, fy+1, fz, fz)
                cor_x.accesspath = [prev_corx]
                @corridors << cor_x

                [-1, 1].each { |diry|
                    @noblesuite ||= -1
                    @noblesuite += 1

                    r = Room.new(:nobleroom, :office, cx-1, cx+1, fy+diry*3, fy+diry*5, fz)
                    r.misc[:noblesuite] = @noblesuite
                    r.accesspath = [cor_x]
                    r.layout << {:item => :chair, :x => 1, :y => 1, :makeroom => true}
                    r.layout << {:item => :chair, :x => 1-dirx, :y => 1}
                    r.layout << {:item => :door, :x => 1, :y => 1-2*diry}
                    # TODO noble furniture
                    #r.layout << {:item => :chest, :x => 1+dirx, :y => 0, :ignore => true}
                    @rooms << r

                    r = Room.new(:nobleroom, :bedroom, cx-1, cx+1, fy+diry*7, fy+diry*9, fz)
                    r.misc[:noblesuite] = @noblesuite
                    r.layout << {:item => :bed, :x => 1, :y => 1, :makeroom => true}
                    r.layout << {:item => :door, :x => 1, :y => 1-2*diry, :internal => true}
                    @rooms << r

                    r = Room.new(:nobleroom, :diningroom, cx-1, cx+1, fy+diry*11, fy+diry*13, fz)
                    r.misc[:noblesuite] = @noblesuite
                    r.layout << {:item => :table, :x => 1, :y => 1, :makeroom => true}
                    r.layout << {:item => :chair, :x => 1+dirx, :y => 1}
                    r.layout << {:item => :door, :x => 1, :y => 1-2*diry, :internal => true}
                    @rooms << r

                    r = Room.new(:nobleroom, :tomb, cx-1, cx+1, fy+diry*15, fy+diry*17, fz)
                    r.misc[:noblesuite] = @noblesuite
                    r.layout << {:item => :coffin, :x => 1, :y => 1, :makeroom => true}
                    r.layout << {:item => :door, :x => 1, :y => 1-2*diry, :internal => true}
                    @rooms << r
                }
            }
        end

        # check that tile is surrounded by solid rock/soil walls
        def map_tile_in_rock(tile)
            (-1..1).all? { |dx| (-1..1).all? { |dy|
                t = tile.offset(dx, dy) and t.shape_basic == :Wall and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or tm == :SOIL)
            } }
        end

        # create a new Corridor from origin to surface, through rock
        # may create multiple chunks to avoid obstacles, all parts are added to @corridors
        # returns an array of Corridors, 1st = origin, last = surface
        def find_corridor_tosurface(origin)
            cor1 = Corridor.new(origin.x, origin.x, origin.y, origin.y, origin.z, origin.z)
            @corridors << cor1

            cor1.z2 += 1 while map_tile_in_rock(cor1.maptile2)

            if out = cor1.maptile2 and ((out.shape_basic != :Ramp and out.shape_basic != :Floor) or
                                        out.shape == :TREE or out.designation.flow_size != 0)
                out2 = spiral_search(out) { |t|
                    t = t.offset(0, 0, 1) while map_tile_in_rock(t)
                    ((t.shape_basic == :Ramp or t.shape_basic == :Floor) and t.shape != :TREE and t.designation.flow_size == 0)
                }

                if out.designation.flow_size > 0
                    # damp stone located
                    cor1.z2 -= 2
                    out2 = out2.offset(0, 0, -2)
                else
                    cor1.z2 -= 1
                    out2 = out2.offset(0, 0, -1)
                end

                cors2 = find_corridor_tosurface(out2)

                if (out2.x - cor1.x).abs > 1 or (out2.y - cor1.y).abs > 1
                    # TODO safe L shape
                    dx = (cor1.x <=> out2.x)
                    dx = 0 if (out2.x - cor1.x).abs <= 1
                    dy = (cor1.y <=> out2.y)
                    dy = 0 if (out2.y - cor1.y).abs <= 1
                    cort = Corridor.new(cor1.x-dx, out2.x+dx, cor1.y-dy, out2.y+dy, cor1.z2, cor1.z2)
                    @corridors << cort
                    cor1.accesspath = [cort]
                    cort.accesspath = [cors2[0]]

                    [cor1, cort, *cors2]
                else
                    cor1.accesspath = [cors2[0]]

                    [cor1, *cors2]
                end
            else
                [cor1]
            end

        end

        def surface_tile_at(t, ty=nil)
            @rangez ||= (0...df.world.map.z_count).to_a.reverse

            if ty
                tx = t
            else
                tx, ty = t.x, t.y 
            end

            if sz = @rangez.find { |z| tt = df.map_tile_at(tx, ty, z) and tsb = tt.shape_basic and (tsb == :Floor or tsb == :Ramp) }
                df.map_tile_at(tx, ty, sz)
            end
        end

        def status
            @tasks.inject(Hash.new(0)) { |h, t| h.update t[0] => h[t[0]]+1 }.inspect
        end

        def categorize_all
            @room_category = {}
            @rooms.each { |r|
                (@room_category[r.type] ||= []) << r
            }
        end

        def find_room(type, &b)
            if @room_category.empty?
                if b
                    @rooms.find { |r| r.type == type and b[r] }
                else
                    @rooms.find { |r| r.type == type }
                end
            else
                if b
                    @room_category[type].to_a.find(&b)
                else
                    @room_category[type].to_a.first
                end
            end
        end

        class Corridor
            attr_accessor :x1, :x2, :y1, :y2, :z1, :z2, :accesspath, :status, :layout, :type
            attr_accessor :owner, :subtype
            attr_accessor :misc
            def w; x2-x1+1; end
            def h; y2-y1+1; end
            def h_z; z2-z1+1; end
            def x; x1+w/2; end
            def y; y1+h/2; end
            def z; z1+h_z/2; end
            def maptile;  df.map_tile_at(x,  y,  z);  end
            def maptile1; df.map_tile_at(x1, y1, z1); end
            def maptile2; df.map_tile_at(x2, y2, z2); end

            def initialize(x1, x2, y1, y2, z1, z2)
                @misc = {}
                @status = :plan
                @accesspath = []
                @layout = []
                @type = :corridor
                x1, x2 = x2, x1 if x1 > x2
                y1, y2 = y2, y1 if y1 > y2
                z1, z2 = z2, z1 if z1 > z2
                @x1, @x2, @y1, @y2, @z1, @z2 = x1, x2, y1, y2, z1, z2
            end

            def dig(mode=nil)
                if mode == :plan
                    plandig = true
                    mode = nil
                end
                (@x1..@x2).each { |x| (@y1..@y2).each { |y| (@z1..@z2).each { |z|
                    if t = df.map_tile_at(x, y, z)
                        next if t.tilemat == :CONSTRUCTION
                        dm = mode || dig_mode(t.x, t.y, t.z)
                        dm = :Default if dm != :No and t.shape == :TREE
                        t.dig dm if ((dm == :DownStair or dm == :Channel) and t.shape != :STAIR_DOWN) or t.shape == :WALL or t.shape == :TREE
                    end
                } } }
                @layout.each { |d|
                    if t = df.map_tile_at(x1+d[:x].to_i, y1+d[:y].to_i, z1+d[:z].to_i)
                        next if t.tilemat == :CONSTRUCTION
                        dm = dig_mode(t.x, t.y, t.z)
                        case d[:item]
                        when :pillar
                            t.dig :No
                            t.dig :Smooth
                        when :well
                            t.dig :Channel if t.shape_basic != :Open
                        else
                            next if plandig
                            t.dig dm if (dm == :DownStair and t.shape != :STAIR_DOWN) or t.shape == :WALL or t.shape == :TREE
                        end
                    end
                }
            end

            def dig_mode(x, y, z)
                return :Default if @type != :corridor
                wantup = wantdown = false
                wantup = true if z < z2 and x >= x1 and x <= x2 and y >= y1 and y <= y2
                wantdown = true if z > z1 and x >= x1 and x <= x2 and y >= y1 and y <= y2
                wantup = true if accesspath.find { |r|
                    r.x1 <= x and r.x2 >= x and r.y1 <= y and r.y2 >= y and r.z2 > z
                }
                wantdown = true if accesspath.find { |r|
                    r.x1 <= x and r.x2 >= x and r.y1 <= y and r.y2 >= y and r.z1 < z
                }
                if wantup
                    wantdown ? :UpDownStair : :UpStair
                else
                    wantdown ? :DownStair : :Default
                end
            end

            def dug?(want=nil)
                holes = []
                @layout.each { |d|
                    if t = df.map_tile_at(x1+d[:x].to_i, y1+d[:y].to_i, z1+d[:z].to_i)
                        next holes << t if d[:item] == :pillar
                        return false if t.shape == :WALL
                    end
                }
                (@x1..@x2).each { |x| (@y1..@y2).each { |y| (@z1..@z2).each { |z|
                    if t = df.map_tile_at(x, y, z)
                        return false if (t.shape == :WALL or (want and t.shape_basic != want)) and not holes.find { |h| h.x == t.x and h.y == t.y and h.z == t.z }
                    end
                } } }
                true
            end
        end

        class Room < Corridor
            def initialize(type, subtype, x1, x2, y1, y2, z)
                super(x1, x2, y1, y2, z, z)
                @type = type
                @subtype = subtype
            end

            def dfbuilding
                df.building_find(misc[:bld_id]) if misc[:bld_id]
            end
        end
    end
end
