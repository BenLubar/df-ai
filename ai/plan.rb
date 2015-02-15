class DwarfAI
    class Plan
        attr_accessor :ai, :tasks
        attr_accessor :manager_taskmax, :manager_maxbacklog,
            :dwarves_per_table, :dwarves_per_farmtile,
            :wantdig_max, :spare_bedroom, :past_initial_phase
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

        def startup
            setup_blueprint
            categorize_all

            digroom find_room(:workshop) { |r| r.subtype == :Masons and r.misc[:workshop_level] == 0 }
            digroom find_room(:workshop) { |r| r.subtype == :Carpenters and r.misc[:workshop_level] == 0 }
            find_room(:workshop) { |r| !r.subtype and r.misc[:workshop_level] == 0 }.subtype = :Masons
            find_room(:workshop) { |r| !r.subtype and r.misc[:workshop_level] == 1 }.subtype = :Masons
            find_room(:workshop) { |r| !r.subtype and r.misc[:workshop_level] == 2 }.subtype = :Masons

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

            want_reupdate = false
            bg = df.onupdate_register('df-ai plan bg') {
                t = @tasks[@bg_idx]
                if not t
                    update if want_reupdate
                    df.onupdate_unregister(bg)
                    next
                end

                bg.description = "df-ai plan bg #{t[0]} #{describe_room(t[1]) if t[1].kind_of?(Corridor)}"
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
                when :construct_stockpile
                    try_construct_stockpile(t[1])
                when :construct_activityzone
                    try_construct_activityzone(t[1])
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
            not @tasks.find { |t|
                case t[0]
                when :monitor_cistern, :checkrooms, :checkidle
                else true
                end
            }
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

            @important_workshops ||= [:Still, :Mechanics, :Farmers, :Quern, :Butchers]
            @important_workshops2 ||= [:Kitchen, :Tanners, :Craftsdwarfs, :Loom, :WoodFurnace, :Smelter]
            # [:Fishery,:Leatherworks,:Clothiers,:Jewelers,:MetalsmithsForge,:SoapMaker,:GlassFurnace]

            # if nothing better to do, order the miners to dig remaining stockpiles, workshops, and a few bedrooms
            ifplan = lambda { |_r| _r if _r and _r.status == :plan }
            freebed = @spare_bedroom
            if r =
                   (st = @important_workshops.shift and
                    find_room(:workshop) { |_r| _r.subtype == st and _r.status == :plan and _r.misc[:workshop_level] == 0 }) ||
                   find_room(:cistern)   { |_r| _r.status == :plan } ||
                   find_room(:well)      { |_r| _r.status == :plan } ||
                   find_room(:infirmary) { |_r| _r.status == :plan } ||
                   (find_room(:cemetary) { |_r| _r.status == :plan } if not find_room(:cemetary) { |_r| _r.status != :plan }) ||
                   (st = @important_workshops2.shift and
                    find_room(:workshop) { |_r| _r.subtype == st and _r.status == :plan and _r.misc[:workshop_level] == 0 }) ||
                   find_room(:pitcage) { |_r| _r.status == :plan } ||
                   find_room(:stockpile) { |_r| _r.misc[:stockpile_level] <= 1 and _r.status == :plan } ||
                   find_room(:workshop)  { |_r| _r.subtype and _r.status == :plan and _r.misc[:workshop_level] == 0 } ||
                   (@fort_entrance if not @fort_entrance.misc[:furnished]) ||
                   (@past_initial_phase = true ; false) ||
                   find_room(:outpost)   { |_r| _r.subtype == :cavern and _r.status == :plan } ||
                   find_room(:workshop)  { |_r| _r.subtype and _r.status == :plan and _r.misc[:workshop_level] == 1 } ||
                   find_room(:bedroom)   { |_r| not _r.owner and ((freebed -= 1) >= 0) and _r.status == :plan } ||
                   find_room(:nobleroom) { |_r| _r.status == :finished and not _r.misc[:furnished] } ||
                   find_room(:bedroom)   { |_r| _r.status == :finished and not _r.misc[:furnished] } ||
                   ifplan[find_room(:dininghall) { |_r| _r.layout.find { |f| f[:users] and f[:users].empty? } }] ||
                   ifplan[find_room(:barracks)   { |_r| _r.layout.find { |f| f[:users] and f[:users].empty? } }] ||
                   find_room(:stockpile) { |_r| _r.misc[:stockpile_level] <= 3 and _r.status == :plan } ||
                   find_room(:workshop)  { |_r| _r.subtype and _r.status == :plan } ||
                   find_room(:cartway)   { |_r| _r.status == :plan } ||
                   find_room(:stockpile) { |_r| _r.status == :plan }
                ai.debug "checkidle #{describe_room(r)}"
                wantdig(r)
                if r.status == :finished
                    r.misc[:furnished] = true
                    r.layout.each { |f| f.delete :ignore }
                    furnish_room(r)
                    smooth_room(r)
                end
                false
            elsif idle?
                if setup_blueprint_caverns
                    ai.debug 'found next cavern'
                    categorize_all
                    false
                else
                    idleidle
                    true
                end
            end
        end

        def idleidle
            ai.debug 'smooth fort'
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

            bg = df.onupdate_register_once('df-ai plan idleidle', 4) {
                if r = tab.shift
                    bg.description = "df-ai plan idleidle #{@rooms.index(r) or @corridors.index(r)} #{r.type} #{r.subtype}"
                    smooth_room(r)
                    false
                else
                    true
                end
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
                    t = df.map_tile_at(r.x1+f[:x].to_i, r.y1+f[:y].to_i, r.z1+f[:z].to_i)
                    if (f[:bld_id] and not df.building_find(f[:bld_id]))
                        ai.debug("fix furniture #{f[:item]} in #{describe_room(r)}", t)
                        f.delete :bld_id
                        @tasks << [:furnish, r, f]
                    end
                    if f[:construction]
                        try_furnish_construction(r, f, t)
                    end
                }
                # tantrumed building
                if r.misc[:bld_id] and not r.dfbuilding
                    ai.debug("rebuild #{describe_room(r)}", r)
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
                ai.debug("assign #{describe_room(r)}", r)
                if r.status == :finished
                    furnish_room(r)
                end
            else
                ai.debug "AI cant getbedroom(#{id})"
            end
        end

        def getdiningroom(id)
            if r = find_room(:farmplot) { |_r| _r.subtype == :food and not _r.misc[:outdoor] and _r.misc[:users].length < _r.w*_r.h*@dwarves_per_farmtile }
                wantdig(r)
                r.misc[:users] << id
            end
            if r = find_room(:farmplot) { |_r| _r.subtype == :cloth and not _r.misc[:outdoor] and _r.misc[:users].length < _r.w*_r.h*@dwarves_per_farmtile }
                wantdig(r)
                r.misc[:users] << id
            end
            if r = find_room(:farmplot) { |_r| _r.subtype == :food and _r.misc[:outdoor] and _r.misc[:users].length < _r.w*_r.h*@dwarves_per_farmtile }
                wantdig(r)
                r.misc[:users] << id
            end
            if r = find_room(:farmplot) { |_r| _r.subtype == :cloth and _r.misc[:outdoor] and _r.misc[:users].length < _r.w*_r.h*@dwarves_per_farmtile }
                wantdig(r)
                r.misc[:users] << id
            end

            if r = find_room(:dininghall) { |_r| _r.layout.find { |f| f[:users] and f[:users].length < @dwarves_per_table } }
                wantdig(r)
                if table = r.layout.find { |f| f[:item] == :table and f[:users].length < @dwarves_per_table }
                    table.delete :ignore
                    table[:users] << id
                end
                if chair = r.layout.find { |f| f[:item] == :chair and f[:users].length < @dwarves_per_table }
                    chair.delete :ignore
                    chair[:users] << id
                end
                if r.status == :finished
                    furnish_room(r)
                end
            end
        end

        def attribute_noblerooms(id_list)
            # XXX tomb may be populated...
            while old = find_room(:nobleroom) { |r| r.owner and not id_list.include?(r.owner) }
                set_owner(old, nil)
            end

            id_list.each { |id|
                entpos = df.unit_entitypositions(df.unit_find(id))
                base = find_room(:nobleroom) { |r| r.owner == id } || find_room(:nobleroom) { |r| not r.owner }
                seen = {}
                while room = find_room(:nobleroom) { |r| r.misc[:noblesuite] == base.misc[:noblesuite] and not seen[r.subtype] }
                    seen[room.subtype] = true
                    set_owner(room, id)
                    wantdig(room) if case room.subtype
                        when :tomb;       entpos.find { |ep| ep.required_tomb    > 0 }
                        when :diningroom; entpos.find { |ep| ep.required_dining  > 0 }
                        when :bedroom;    entpos.find { |ep| ep.required_bedroom > 0 }
                        when :office;     entpos.find { |ep| ep.required_office  > 0 }
                        end
                end
            }
        end

        def getsoldierbarrack(id)
            u = df.unit_find(id)
            return if not u
            squad_id = u.military.squad_id
            return if squad_id == -1

            if not r = find_room(:barracks) { |_r| _r.misc[:squad_id] == squad_id }
                r = find_room(:barracks) { |_r| not _r.misc[:squad_id] }
                if not r
                    ai.debug "no free barracks"
                    return
                end

                r.misc[:squad_id] = squad_id
                ai.debug("squad #{squad_id} assign #{describe_room(r)}", r)
                wantdig(r)
                if r.misc[:bld_id] and bld = r.dfbuilding
                    assign_barrack_squad(bld, squad_id)
                end
            end

            [:weaponrack, :armorstand, :bed, :cabinet, :chest, :archerytarget].map { |it|
                r.layout.find { |f| f[:item] == it and f[:users].include?(id) } ||
                r.layout.find { |f| f[:item] == it and (it == :archerytarget or f[:users].length < (it == :weaponrack ? 4 : 1)) }
            }.compact.each { |f|
                f[:users] << id unless f[:users].include?(id)
                f.delete :ignore
            }
            if r.status == :finished
                furnish_room(r)
            end
        end

        def assign_barrack_squad(bld, squad_id)
            if bld.respond_to?(:squads) # archerytarget has no such field
                su = bld.squads.find { |_su| _su.squad_id == squad_id }
                if not su
                    su = DFHack::BuildingSquadUse.cpp_new(:squad_id => squad_id)
                    bld.squads << su
                end
                su.mode.sleep = true
                su.mode.train = true
                su.mode.indiv_eq = true
                su.mode.squad_eq = true
            end

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
                ai.debug("free #{describe_room(r)}", r)
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
                                    ai.debug("squad #{r.misc[:squad_id]} free #{describe_room(r)}", r)
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
            limit = 1000 - ((11*11*1000 / df.unit_find(pet_id).caste_tg.misc.grazer) rescue 0) # 1000 = arbitrary, based on dfwiki?pasture
            if r = find_room(:pasture) { |_r|
                _r.misc[:users].empty? or
                _r.misc[:users].inject(0) { |s, id|
                    # 11*11 == pasture dimensions
                    s + ((11*11*1000 / df.unit_find(id).caste_tg.misc.grazer) rescue 0)
                } < limit
            }
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
            ai.debug "wantdig #{describe_room(r)}"
            r.misc[:queue_dig] = true
            r.dig(:plan)
            @tasks << [:wantdig, r]
        end

        def digroom(r)
            return true if r.status != :plan
            ai.debug "digroom #{describe_room(r)}"
            r.misc.delete :queue_dig
            r.status = :dig
            r.fixup_open
            r.dig
            @tasks << [:digroom, r]
            r.accesspath.each { |ap| digroom(ap) }

            r.layout.each { |f|
                next if f[:item] == :floodgate
                next if f[:dig]
                @tasks << [:furnish, r, f]
            }

            if r.type == :workshop and r.misc[:workshop_level] == 0
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
                    sp.misc[:stockpile_level] = 0
                    @rooms << sp
                    categorize_all
                    digroom(sp)
                end
            end

            if r.type != :corridor or r.h_z > 1
                @nrdig += 1
                @nrdig += 1 if r.w*r.h*r.h_z >= 10
            end

            true
        end

        def construct_room(r)
            ai.debug "construct #{describe_room(r)}"
            case r.type
            when :corridor
                furnish_room(r)
            when :stockpile
                furnish_room(r)
                @tasks << [:construct_stockpile, r]
            when :workshop
                @tasks << [:construct_workshop, r]
            when :farmplot
                construct_farmplot(r)
            when :cistern
                construct_cistern(r)
            when :cemetary
                furnish_room(r)
            when :infirmary, :pasture, :pitcage
                furnish_room(r)
                @tasks << [:construct_activityzone, r] unless try_construct_activityzone(r)
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
            :gear_assembly => :GearAssembly,
            :vertical_axle => :AxleVertical,
            :traction_bench => :TractionBench,
            :nestbox => :NestBox

        def try_furnish(r, f)
            return true if f[:bld_id]
            return true if f[:ignore]
            tgtile = df.map_tile_at(r.x1+f[:x].to_i, r.y1+f[:y].to_i, r.z1+f[:z].to_i)
            return if not tgtile
            if f[:construction]
                if try_furnish_construction(r, f, tgtile)
                    return true if not f[:item]
                else
                    return  # dont try to furnish item before construction is done
                end
            end
            return if tgtile.shape_basic == :Wall
            case f[:item]
            when nil
                return true
            when :well
                return try_furnish_well(r, f, tgtile)
            when :archerytarget
                return try_furnish_archerytarget(r, f, tgtile)
            when :gear_assembly
                return if not itm = df.world.items.other[:TRAPPARTS].find { |i| ai.stocks.is_item_free(i) }
            when :vertical_axle
                return if not itm = df.world.items.other[:WOOD].find { |i| ai.stocks.is_item_free(i) }
            when :windmill
                return try_furnish_windmill(r, f, tgtile)
            when :roller
                return try_furnish_roller(r, f, tgtile)
            when :minecart_route
                return try_furnish_minecart_route(r, f, tgtile)
            end

            return if @cache_nofurnish[f[:item]]
            if tgtile.occupancy.building != :None
                # TODO warn if this stays for too long?
                false
            elsif tgtile.shape == :RAMP or tgtile.tilemat == :TREE or tgtile.tilemat == :ROOT
                tgtile.dig(f[:dig] || :Default)
                false
            elsif itm ||= ai.stocks.find_furniture_item(f[:item])
                return if f[:subtype] == :cage and ai.stocks.count[:cage].to_i < 1  # avoid too much spam
                ai.debug "furnish #{f[:item]} in #{describe_room(r)}"
                bldn = FurnitureBuilding[f[:item]]
                subtype = { :cage => :CageTrap, :lever => :Lever, :trackstop => :TrackStop }.fetch(f[:subtype], -1)
                bld = df.building_alloc(bldn, subtype)
                df.building_position(bld, tgtile)
                f[:misc_bldprops].each { |k, v| bld.send("#{k}=", v) } if f[:misc_bldprops]
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

        def try_furnish_well(r, f, t)
            if block = df.world.items.other[:BLOCKS].find { |i| ai.stocks.is_item_free(i) } and
               mecha = df.world.items.other[:TRAPPARTS].find { |i| ai.stocks.is_item_free(i) } and
               buckt = df.world.items.other[:BUCKET].find { |i| ai.stocks.is_item_free(i) } and
               chain = df.world.items.other[:CHAIN].find { |i| ai.stocks.is_item_free(i) }
                bld = df.building_alloc(:Well)
                df.building_position(bld, t)
                df.building_construct(bld, [block, mecha, buckt, chain])
                r.misc[:bld_id] = f[:bld_id] = bld.id
                @tasks << [:checkfurnish, r, f]
                true
            end
        end

        def try_furnish_archerytarget(r, f, t)
            if bould = df.world.items.other[:BOULDER].find { |i|
                    ai.stocks.is_item_free(i) and !df.ui.economic_stone[i.mat_index]
                }
                bld = df.building_alloc(:ArcheryTarget)
                bld.archery_direction = (f[:y] > 2 ? :TopToBottom : :BottomToTop)
                df.building_position(bld, t)
                df.building_construct(bld, [bould])
                f[:bld_id] = bld.id
                @tasks << [:checkfurnish, r, f]
                true
            end
        end

        def try_furnish_construction(r, f, t)
            if t.tilemat == :TREE
                DwarfAI::Plan.find_tree_base(t).dig
                return
            end
            case ctype = f[:construction]
            when :NoRamp
                t.dig if t.shape_basic == :Ramp
                return (t.designation.dig == :No)
            when :Wall
                return true if t.shape_basic == :Wall
            when :Ramp
                return true if t.shape_basic == :Ramp
            when :UpStair, :DownStair, :UpDownStair
                return true if t.shape_basic == :Stair
            when :Floor
                return true if t.shape_basic == :Floor
                if t.shape_basic == :Ramp
                    t.dig
                    return true
                end

            when :Track, :TrackRamp
                wantdir = f[:dir].sort_by { |d| [:n, :s, :e, :w].index(d) }.join.upcase
                wantshape = :Floor
                wantshape = :Ramp if ctype == :TrackRamp

                if t.special == :TRACK and t.direction == wantdir and t.shape_basic == wantshape
                    return true
                end

                case t.tilemat
                when :MINERAL, :STONE
                    if ctype == :Track and t.shape_basic == :Ramp
                        t.dig
                        return
                    end

                    if t.shape_basic == wantshape
                        # carve track
                        t.occupancy.carve_track_north = true if f[:dir].include?(:n)
                        t.occupancy.carve_track_south = true if f[:dir].include?(:s)
                        t.occupancy.carve_track_east  = true if f[:dir].include?(:e)
                        t.occupancy.carve_track_west  = true if f[:dir].include?(:w)
                        t.mapblock.flags.designated = true
                        return true
                    end
                end

                ctype = (ctype.to_s + wantdir).to_sym

            else
                raise "ai plan: invalid construction type #{ctype.inspect}"
            end

            # fall through = must build actual construction

            if t.tilemat == :CONSTRUCTION
                # remove existing invalid construction
                t.dig
                return
            end

            return if df.world.buildings.all.any? { |b|
                b.z == t.z and
                not b.room.extents and
                b.x1 <= t.x and b.x2 >= t.x and
                b.y1 <= t.y and b.y2 >= t.y
            }

            if block = df.world.items.other[:BLOCKS].find { |i| ai.stocks.is_item_free(i) }
                bld = df.building_alloc(:Construction, ctype)
                df.building_position(bld, t)
                df.building_construct(bld, [block])
                true
            end
        end

        def try_furnish_windmill(r, f, t)
            return if t.shape_basic != :Open
            mat = (1..4).map {
                df.world.items.other[:WOOD].find { |i| ai.stocks.is_item_free(i) }
            }
            if mat.compact.length == 4
                bld = df.building_alloc(:Windmill)
                df.building_position(bld, t.offset(-1, -1), 3, 3)
                df.building_construct(bld, mat)
                f[:bld_id] = bld.id
                @tasks << [:checkfurnish, r, f]
                true
            end
        end

        def try_furnish_roller(r, f, t)
            if mecha = df.world.items.other[:TRAPPARTS].find { |i| ai.stocks.is_item_free(i) } and
               chain = df.world.items.other[:CHAIN].find { |i| ai.stocks.is_item_free(i) }
                bld = df.building_alloc(:Rollers)
                df.building_position(bld, t)
                f[:misc_bldprops].each { |k, v| bld.send("#{k}=", v) } if f[:misc_bldprops]
                df.building_construct(bld, [mecha, chain])
                r.misc[:bld_id] = f[:bld_id] = bld.id
                @tasks << [:checkfurnish, r, f]
                true
            end
        end

        def try_furnish_minecart_route(r, f, t)
            return true if f[:route_id]
            return if not r.dfbuilding
            # TODO wait roller ?
            if mcart = df.world.items.other[:TOOL].find { |i|
                i.subtype.subtype == ai.stocks.class::ManagerSubtype[:MakeWoodenMinecart] and
                i.stockpile.id == -1 and
                (!i.vehicle_tg or i.vehicle_tg.route_id == -1)
            }
                routelink = DFHack::RouteStockpileLink.cpp_new(:building_id => r.misc[:bld_id], :mode => { :take => true })
                stop = DFHack::HaulingStop.cpp_new(:id => 1, :pos => t)
                cond = DFHack::StopDepartCondition.cpp_new(:direction => f[:direction], :mode => :Push,
                        :load_percent => 100, :flags => {})
                stop.conditions << cond
                stop.stockpiles << routelink
                stop.cart_id = mcart.id
                setup_stockpile_settings(r.subtype, stop)
                route = DFHack::HaulingRoute.cpp_new(:id => df.ui.hauling.next_id)
                route.stops << stop
                route.vehicle_ids << mcart.vehicle_id
                route.vehicle_stops << 0
                vehicle = mcart.vehicle_tg
                vehicle.route_id = route.id
                r.dfbuilding.linked_stops << stop
                df.ui.hauling.next_id += 1
                df.ui.hauling.routes << route
                #view_* are gui only
                f[:route_id] = route.id
                true
            end
        end

        def try_furnish_trap(r, f)
            t = df.map_tile_at(r.x1+f[:x].to_i, r.y1+f[:y].to_i, r.z1+f[:z].to_i)

            if t.occupancy.building != :None
                return
            elsif t.shape == :RAMP or t.tilemat == :TREE or t.tilemat == :ROOT
                # XXX dont remove last access ramp ?
                t.dig(:Default)
                return
            end

            if mecha = df.world.items.other[:TRAPPARTS].find { |i| ai.stocks.is_item_free(i) }
                subtype = {:lever => :Lever, :cage => :CageTrap}.fetch(f[:subtype])
                bld = df.building_alloc(:Trap, subtype)
                df.building_position(bld, t)
                df.building_construct(bld, [mecha])
                f[:bld_id] = bld.id
                @tasks << [:checkfurnish, r, f]
                true
            end
        end

        def try_construct_workshop(r)
            return unless r.constructions_done?

            case r.subtype
            when :Dyers
                # barrel, bucket
                if barrel = df.world.items.other[:BARREL].find { |i| ai.stocks.is_item_free(i) } and
                   bucket = df.world.items.other[:BUCKET].find { |i| ai.stocks.is_item_free(i) }
                    bld = df.building_alloc(:Workshop, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [barrel, bucket])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :Ashery
                # block, barrel, bucket
                if block  = df.world.items.other[:BLOCKS].find { |i| ai.stocks.is_item_free(i) } and
                   barrel = df.world.items.other[:BARREL].find { |i| ai.stocks.is_item_free(i) } and
                   bucket = df.world.items.other[:BUCKET].find { |i| ai.stocks.is_item_free(i) }
                    bld = df.building_alloc(:Workshop, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [block, barrel, bucket])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :SoapMaker
                # bucket, boulder
                if buckt = df.world.items.other[:BUCKET].find { |i| ai.stocks.is_item_free(i) } and
                   bould = df.world.items.other[:BOULDER].find { |i|
                       ai.stocks.is_item_free(i) and !df.ui.economic_stone[i.mat_index] and i.isTemperatureSafe(11640)
                   }
                    custom = df.world.raws.buildings.all.find { |b| b.code == 'SOAP_MAKER' }.id
                    bld = df.building_alloc(:Workshop, :Custom, custom)
                    df.building_position(bld, r)
                    df.building_construct(bld, [buckt, bould])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :ScrewPress
                # mechanism*2
                mechas = df.world.items.other[:TRAPPARTS].find_all { |i| ai.stocks.is_item_free(i) }[0, 2]
                if mechas.length == 2
                    custom = df.world.raws.buildings.all.find { |b| b.code == 'SCREW_PRESS' }.id
                    bld = df.building_alloc(:Workshop, :Custom, custom)
                    df.building_position(bld, r)
                    df.building_construct(bld, mechas)
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :MetalsmithsForge
                # anvil, boulder
                if anvil = df.world.items.other[:ANVIL].find { |i|
                        ai.stocks.is_item_free(i) and i.isTemperatureSafe(11640)
                } and bould = df.world.items.other[:BOULDER].find { |i|
                        ai.stocks.is_item_free(i) and !df.ui.economic_stone[i.mat_index] and i.isTemperatureSafe(11640)
                }
                    bld = df.building_alloc(:Workshop, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [anvil, bould])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :WoodFurnace, :Smelter, :Kiln, :GlassFurnace
                # firesafe boulder
                if bould = df.world.items.other[:BOULDER].find { |i|
                        ai.stocks.is_item_free(i) and !df.ui.economic_stone[i.mat_index] and i.isTemperatureSafe(11640)
                }
                    bld = df.building_alloc(:Furnace, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [bould])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :Quern
                if quern = df.world.items.other[:QUERN].find { |i| ai.stocks.is_item_free(i) }
                    bld = df.building_alloc(:Workshop, r.subtype)
                    df.building_position(bld, r)
                    df.building_construct(bld, [quern])
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            when :TradeDepot
                boulds = df.world.items.other[:BOULDER].find_all { |i| !df.ui.economic_stone[i.mat_index] and ai.stocks.is_item_free(i) }[0, 3]
                if boulds.length == 3
                    bld = df.building_alloc(:TradeDepot)
                    df.building_position(bld, r)
                    df.building_construct(bld, boulds)
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    true
                end
            else
                # any non-eco boulder
                if bould = df.map_tile_at(r).mapblock.items_tg.find { |i|
                        # check map_block.items first
                        i.kind_of?(DFHack::ItemBoulderst) and
                        ai.stocks.is_item_free(i) and !df.ui.economic_stone[i.mat_index] and
                        i.pos.x >= r.x1 and i.pos.x <= r.x2 and i.pos.y >= r.y1 and i.pos.y <= r.y2
                } || df.world.items.other[:BOULDER].find { |i|
                        ai.stocks.is_item_free(i) and !df.ui.economic_stone[i.mat_index]
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

        def try_construct_stockpile(r)
            return unless r.constructions_done?

            bld = df.building_alloc(:Stockpile)
            df.building_position(bld, r)
            bld.room.extents = df.malloc(r.w*r.h)
            bld.room.x = r.x1
            bld.room.y = r.y1
            bld.room.width = r.w
            bld.room.height = r.h
            r.w.times { |x| r.h.times { |y|
                t = r.maptile1.offset(x, y)
                bld.room.extents[x+r.w*y] = (t.shape_basic == :Floor ? 1 : 0)
            } }
            bld.is_room = 1
            df.building_construct_abstract(bld)
            r.misc[:bld_id] = bld.id
            furnish_room(r)

            setup_stockpile_settings(r.subtype, bld, r)

            room_items(r) { |i| i.flags.dump = true } if r.misc[:workshop] and r.subtype != :stone

            if r.misc[:stockpile_level] == 0 and not (r.subtype == :stone or r.subtype == :wood)
                if rr = find_room(:stockpile) { |o| o.subtype == r.subtype and o.misc[:stockpile_level] == 1 }
                    wantdig(rr)
                end
            end

            # setup stockpile links with adjacent stockpile_level
            find_room(:stockpile) { |o|
                if o.subtype == r.subtype and (o.misc[:stockpile_level] - r.misc[:stockpile_level]).abs == 1 and obld = o.dfbuilding
                    if o.misc[:stockpile_level] > r.misc[:stockpile_level]
                        b_from = obld
                        b_to = bld
                    else
                        b_from = bld
                        b_to = obld
                    end
                    next if b_to.links.take_from_pile.find { |btf| btf.id == b_from.id }
                    b_to.links.take_from_pile << b_from
                    b_from.links.give_to_pile << b_to
                    false   # loop on all stockpiles
                end
            }

            true
        end

        def try_construct_activityzone(r)
            return unless r.constructions_done?

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
            when :garbagedump
                bld.zone_flags.garbage_dump = true
            when :pasture
                bld.zone_flags.pen_pasture = true
                # pit_flags |= 2
            when :pitcage
                bld.zone_flags.pit_pond = true
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

            true
        end

        def setup_stockpile_settings(subtype, bld, r=nil)
            case subtype
            when :stone
                bld.settings.flags.stone = true
                t = bld.settings.stone.mats
                if r and r.misc[:workshop] and r.misc[:workshop].subtype == :Masons
                    df.world.raws.inorganics.length.times { |i|
                        t[i] = (df.ui.economic_stone[i] ? 0 : 1)
                    }
                elsif r and r.misc[:workshop] and r.misc[:workshop].subtype == :Smelter
                    df.world.raws.inorganics.length.times { |i|
                        t[i] = (df.world.raws.inorganics[i].flags[:METAL_ORE] ? 1 : 0)
                    }
                else
                    df.world.raws.inorganics.length.times { |i| t[i] = 1 }
                end
                bld.max_wheelbarrows = 1 if r
            when :wood
                bld.settings.flags.wood = true
                t = bld.settings.wood.mats
                df.world.raws.plants.all.length.times { |i| t[i] = 1 }
            when :furniture, :finished_goods, :ammo, :weapons, :armor
                case subtype
                when :furniture
                    bld.settings.flags.furniture = true
                    bld.settings.furniture.sand_bags = true
                    s = bld.settings.furniture
                    DFHack::FurnitureType::ENUM.length.times { |i| s.type[i] = true }
                when :finished_goods
                    bld.settings.flags.finished_goods = true
                    s = bld.settings.finished_goods
                    DFHack::ItemType::ENUM.length.times { |i| s.type[i] = true }
                    bld.max_bins = r.w*r.h if r
                when :ammo
                    bld.settings.flags.ammo = true
                    s = bld.settings.ammo
                    df.world.raws.itemdefs.ammo.length.times { |i| s.type[i] = true }
                    bld.max_bins = r.w*r.h if r
                when :weapons
                    bld.settings.flags.weapons = true
                    s = bld.settings.weapons
                    df.world.raws.itemdefs.weapons.length.times { |i| s.weapon_type[i] = true }
                    df.world.raws.itemdefs.trapcomps.length.times { |i| s.trapcomp_type[i] = true }
                    s.usable = true
                    s.unusable = true
                    bld.max_bins = r.w*r.h if r
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
                    bld.max_bins = r.w*r.h if r
                end
                30.times { |i| s.other_mats[i] = true }
                df.world.raws.inorganics.length.times { |i| s.mats[i] = true }
                s.quality_core.map! { true }
                s.quality_total.map! { true }
            when :animals
                bld.settings.flags.animals = true
                bld.settings.animals.empty_cages =
                bld.settings.animals.empty_traps = (!r || r.misc[:stockpile_level] > 1 ? true : false)
                df.world.raws.creatures.all.length.times { |i| bld.settings.animals.enabled[i] = true }
            when :refuse, :corpses
                bld.settings.flags.refuse = true
                bld.settings.refuse.fresh_raw_hide = false
                bld.settings.refuse.rotten_raw_hide = true
                t = bld.settings.refuse
                if r and r.misc[:workshop] and r.misc[:workshop].subtype == :Craftsdwarfs
                    df.world.raws.creatures.all.length.times { |i|
                        t.corpses[i] = t.body_parts[i] = t.hair[i] = false
                        t.skulls[i] = t.bones[i] = t.shells[i] = t.teeth[i] = t.horns[i] = true
                    }
                elsif subtype == :corpses
                    DFHack::ItemType::ENUM.length.times { |i| t.type[i] = true }
                    t.type[DFHack::ItemType::NUME[:REMAINS]] = t.type[DFHack::ItemType::NUME[:PLANT]] = t.type[DFHack::ItemType::NUME[:PLANT_GROWTH]] = false
                    df.world.raws.creatures.all.length.times { |i|
                        t.body_parts[i] = t.corpses[i] = true
                        t.skulls[i] = t.bones[i] = t.hair[i] = t.shells[i] = t.teeth[i] = t.horns[i] = false
                    }
                else
                    bld.settings.refuse.fresh_raw_hide = true
                    bld.settings.refuse.rotten_raw_hide = false
                    df.world.raws.creatures.all.length.times { |i|
                        t.corpses[i] = t.body_parts[i] = false
                        t.skulls[i] = t.bones[i] = t.hair[i] = t.shells[i] = t.teeth[i] = t.horns[i] = true
                    }
                end
            when :food
                bld.settings.flags.food = true
                bld.max_barrels = r.w*r.h if r
                t = bld.settings.food
                mt = df.world.raws.mat_table
                if r and r.misc[:workshop] and r.misc[:workshop].type == :farmplot
                    mt.organic_types[:Seed].length.times { |i| t.seeds[i] = true }
                elsif r and r.misc[:workshop] and r.misc[:workshop].subtype == :Farmers
                    mt.organic_types[:Plants].length.times { |i|
                        # include MILL because the quern is near
                        plant = df.decode_mat(mt.organic_types[:Plants][i], mt.organic_indexes[:Plants][i]).plant
                        t.plants[i] = (plant.flags[:THREAD] or plant.flags[:MILL]) if plant
                    }
                elsif r and r.misc[:workshop] and r.misc[:workshop].subtype == :Still
                    mt.organic_types[:Plants].length.times { |i|
                        plant = df.decode_mat(mt.organic_types[:Plants][i], mt.organic_indexes[:Plants][i]).plant
                        t.plants[i] = plant.flags[:DRINK] if plant
                    }
                    mt.organic_types[:Leaf].length.times { |i|
                        plant = df.decode_mat(mt.organic_types[:Leaf][i], mt.organic_indexes[:Leaf][i]).plant
                        t.leaves[i] = plant.flags[:DRINK] if plant
                    }
                elsif r and r.misc[:workshop] and r.misc[:workshop].subtype == :Kitchen
                    mt.organic_types[:Meat          ].length.times { |i| t.meat[i]            = true }
                    mt.organic_types[:Fish          ].length.times { |i| t.fish[i]            = true }
                    mt.organic_types[:Eggs          ].length.times { |i| t.egg[i]             = true }
                    mt.organic_types[:Leaf          ].length.times { |i| t.leaves[i]          = true }
                elsif r and r.misc[:workshop] and r.misc[:workshop].subtype == :Fishery
                    mt.organic_types[:UnpreparedFish].length.times { |i| t.unprepared_fish[i] = true }
                else
                    bld.settings.food.prepared_meals = true
                    mt.organic_types[:Meat          ].length.times { |i| t.meat[i]            = true }
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
                bld.max_bins = r.w*r.h if r
                t = bld.settings.cloth
                if r and r.misc[:workshop] and r.misc[:workshop].subtype == :Loom
                    df.world.raws.mat_table.organic_types[:Silk       ].length.times { |i| t.thread_silk[i]  = true ; t.cloth_silk[i]  = false }
                    df.world.raws.mat_table.organic_types[:PlantFiber ].length.times { |i| t.thread_plant[i] = true ; t.cloth_plant[i] = false }
                    df.world.raws.mat_table.organic_types[:Yarn       ].length.times { |i| t.thread_yarn[i]  = true ; t.cloth_yarn[i]  = false }
                    df.world.raws.mat_table.organic_types[:MetalThread].length.times { |i| t.thread_metal[i] = false; t.cloth_metal[i] = false }
                elsif r and r.misc[:workshop] and r.misc[:workshop].subtype == :Clothiers
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
                bld.max_bins = r.w*r.h if r
                t = bld.settings.leather.mats
                df.world.raws.mat_table.organic_types[:Leather].length.times { |i| t[i] = true }
            when :gems
                bld.settings.flags.gems = true
                bld.max_bins = r.w*r.h if r
                t = bld.settings.gems
                if r and r.misc[:workshop] and r.misc[:workshop].subtype == :Jewelers
                    df.world.raws.mat_table.builtin.length.times { |i| t.rough_other_mats[i] = t.cut_other_mats[i] = false }
                    df.world.raws.inorganics.length.times { |i| t.rough_mats[i] = true ; t.cut_mats[i] = false }
                else
                    df.world.raws.mat_table.builtin.length.times { |i| t.rough_other_mats[i] = t.cut_other_mats[i] = true }
                    df.world.raws.inorganics.length.times { |i| t.rough_mats[i] = t.cut_mats[i] = true }
                end
            when :bars_blocks
                bld.settings.flags.bars_blocks = true
                bld.max_bins = r.w*r.h if r
                s = bld.settings.bars_blocks
                if r and r.misc[:workshop] and r.misc[:workshop].subtype == :MetalsmithsForge
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

        MudType  = DFHack::BuiltinMats.int(:MUD)
        MudIndex = -1

        def construct_farmplot(r)
            (r.x1..r.x2).each { |x| (r.y1..r.y2).each { |y| (r.z1..r.z2).each { |z|
                if t = df.map_tile_at(x, y, z) and not [:GRASS_DARK, :GRASS_LIGHT, :SOIL].include?(t.tilemat)
                    if not e = t.mapblock.block_events.find { |_e|
                        _e.kind_of?(DFHack::BlockSquareEventMaterialSpatterst) and _e.mat_type == MudType and _e.mat_index == MudIndex
                    }
                        e = DFHack::BlockSquareEventMaterialSpatterst.cpp_new(:mat_type => MudType, :mat_index => MudIndex)
                        e.min_temperature = e.max_temperature = 60001
                        t.mapblock.block_events << e
                    end
                    if e.amount[t.dx][t.dy] < 50
                        ai.debug "cheat: mud invocation (#{x}, #{y}, #{z})"
                        e.amount[t.dx][t.dy] = 50 # small pile of mud
                    end
                end
            } } }

            bld = df.building_alloc(:FarmPlot)
            df.building_position(bld, r)
            df.building_construct(bld, [])
            r.misc[:bld_id] = bld.id
            furnish_room(r)
            if st = find_room(:stockpile) { |_r| _r.misc[:workshop] == r }
                digroom(st)
            end
            @tasks << [:setup_farmplot, r]
        end

        def move_dininghall_fromtemp(r, t)
            # if we dug a real hall, get rid of the temporary one
            t.layout.each { |f|
                if of = r.layout.find { |_f| _f[:item] == f[:item] and _f[:users] == [] }
                    of[:users] = f[:users]
                    of.delete :ignore unless f[:ignore]
                    if f[:bld_id] and bld = df.building_find(f[:bld_id])
                        df.building_deconstruct(bld)
                    end
                end
            }
            @rooms.delete t
            categorize_all
        end

        def smooth_room(r)
            smooth_xyz!((r.x1-1)..(r.x2+1), (r.y1-1)..(r.y2+1), (r.z1)..(r.z2))
        end

        # smooth a room and its accesspath corridors (recursive)
        def smooth_room_access(r)
            smooth_room(r)
            r.accesspath.each { |a| smooth_room_access(a) }
        end

        def smooth_cistern(r)
            r.accesspath.each { |a| smooth_room_access(a) }

            tiles = []
            (r.z1..r.z2).each { |z|
                ((r.x1-1)..(r.x2+1)).each { |x|
                    ((r.y1-1)..(r.y2+1)).each { |y|
                        next if z != r.z1 and r.x1 <= x and r.x2 >= x and r.y1 <= y and r.y2 >= y
                        next unless t = df.map_tile_at(x, y, z)
                        tiles << t
                    }
                }
            }
            smooth!(*tiles)
        end

        def construct_cistern(r)
            furnish_room(r)
            smooth_cistern(r)

            # remove boulders
            dump_items_access(r)

            # build levers for floodgates
            wantdig find_room(:well)

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

        def smooth_xyz!(xs, ys, zs)
            tiles = []
            xs.each { |x|
                ys.each { |y|
                    zs.each { |z|
                        tiles << [x, y, z]
                    }
                }
            }
            smooth!(*tiles)
        end

        def smooth!(*tiles)
            # get df tile references
            tiles = tiles.map { |tile|
                if Array === tile
                    df.map_tile_at(*tile)
                else
                    df.map_tile_at(tile)
                end
            }

            # remove tiles that are not smoothable
            tiles = tiles.reject { |tile|
                next true unless tile

                # not a smoothable material
                next true if tile.tilemat != :STONE and tile.tilemat != :MINERAL

                # already designated for something
                next true if tile.designation.dig != :No or tile.designation.smooth != 0 or tile.designation.hidden

                # already smooth
                next true if smooth?(tile)

                # wrong shape
                next true if tile.shape_basic != :Wall and tile.shape_basic != :Floor
            }

            # remove tiles that are already being smoothed
            df.world.job_list.each { |j|
                if j.job_type == :DetailWall or j.job_type == :DetailFloor
                    tiles = tiles.reject { |tile|
                        df.same_pos?(j, tile)
                    }
                end
            }

            # mark the tiles to be smoothed!
            tiles.each { |tile|
                tile.designation.smooth = 1
                tile.mapblock.flags.designated = true
            }
        end

        def smooth?(t)
            return if not t
            t.tilemat == :SOIL or
            t.tilemat == :GRASS_LIGHT or
            t.tilemat == :GRASS_DARK or
            t.tilemat == :PLANT or
            t.tilemat == :ROOT or
            t.special == :TRACK or
            t.special == :SMOOTH or
            t.tiletype == :FORTIFICATION or
            t.shape_basic == :Open or
            t.shape_basic == :Stair or
            t.occupancy.building != :None
        end

        # check smoothing progress, channel intermediate floors when done
        def try_digcistern(r)
            # XXX hardcoded layout..
            cnt = 0
            acc_y = r.accesspath[0].y1
            ((r.z1+1)..r.z2).each { |z|
                (r.x1..r.x2).to_a.reverse.each { |x|
                    stop = false
                    (r.y1..r.y2).each { |y|
                        next unless t = df.map_tile_at(x, y, z)
                        case t.shape_basic
                        when :Floor
                            if not smooth?(df.map_tile_at(x+1, y-1, z)) or
                                not smooth?(df.map_tile_at(x+1, y, z)) or
                                not smooth?(df.map_tile_at(x+1, y+1, z)) or
                                not nt01 = df.map_tile_at(x-1, y, z)
                                stop = true
                                next
                            end
                            if nt01.shape_basic == :Floor
                                t.dig :Channel
                            else
                                # last column before stairs
                                next unless nt10 = df.map_tile_at(x, y-1, z)
                                next unless nt12 = df.map_tile_at(x, y+1, z)
                                next unless nt00 = df.map_tile_at(x-1, y-1, z)
                                next unless nt02 = df.map_tile_at(x-1, y+1, z)
                                t.dig :Channel if (y > acc_y ? smooth?(nt12) && smooth?(nt02) : smooth?(nt10) && smooth?(nt00))
                            end
                        when :Open
                            cnt += 1 if x >= r.x1 and x <= r.x2 and y >= r.y1 and y <= r.y2
                        end
                    }
                    break if stop
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
                        try_construct_activityzone(r)
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
            return true if not bld
            return if bld.getBuildStage < bld.getMaxBuildStage

            ai.stocks.farmplot(r)

            true
        end

        def try_endfurnish(r, f)
            bld = df.building_find(f[:bld_id]) if f[:bld_id]
            if not bld
                #ai.debug "destroyed building ? #{r.inspect} #{f.inspect}"
                return true
            end
            return if bld.getBuildStage < bld.getMaxBuildStage

            case f[:item]
            when :coffin
                bld.burial_mode.allow_burial = true
                bld.burial_mode.no_citizens = false
                bld.burial_mode.no_pets = true
            when :door
                bld.door_flags.pet_passable = true
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
            when :archerytarget
                f[:makeroom] = true
            end

            return true unless f[:makeroom]
            return unless r.dug?

            ai.debug "makeroom #{describe_room(r)}"

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
            r.layout.each { |f_|
                next if f_[:item] != :door
                x = r.x1 + f_[:x].to_i
                y = r.y1 + f_[:y].to_i
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
                bld = r.dfbuilding
                bld = df.building_find(f[:bld_id]) if f[:item] == :archerytarget
                if squad_id = r.misc[:squad_id] and bld
                    assign_barrack_squad(bld, squad_id)
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
            return if bld.general_refs.find { |ref| ref.kind_of?(DFHack::GeneralRefBuildingTriggertargetst) and
                    ref.building_id == tbld.id }
            return if bld.jobs.find { |j| j.job_type == :LinkBuildingToTrigger and
                j.general_refs.find { |ref| ref.kind_of?(DFHack::GeneralRefBuildingTriggertargetst) and
                    ref.building_id == tbld.id }
            }

            mechas = df.world.items.other[:TRAPPARTS].find_all { |i| ai.stocks.is_item_free(i) }[0, 2]
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
            if not bld
                ai.debug "cistern: missing lever #{f[:way]}"
                return
            end
            if not bld.jobs.empty?
                ai.debug "cistern: lever has job #{f[:way]}"
                return
            end
            ai.debug "cistern: pull lever #{f[:way]}"

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

            if l_in and not f_out and l_in.linked_mechanisms.first
                # f_in is linked, can furnish f_out now without risking walling workers in the reserve
                @m_c_reserve.layout.each { |l| l.delete :ignore }
                furnish_room @m_c_reserve
            end

            return unless l_in and f_in and l_out and f_out
            return unless l_in.linked_mechanisms.first and l_out.linked_mechanisms.first
            return unless l_in.jobs.empty? and l_out.jobs.empty?

            if @fort_entrance.layout.find { |f| f[:ignore] }
                # may use mechanisms for entrance cage traps now
                @fort_entrance.layout.each { |ef| ef.delete :ignore }
                furnish_room(@fort_entrance)
            end

            f_in_closed = true if f_in.gate_flags.closed or f_in.gate_flags.closing
            f_in_closed = false if f_in.gate_flags.opening
            f_out_closed = true if f_out.gate_flags.closed or f_out.gate_flags.closing
            f_out_closed = false if f_out.gate_flags.opening

            if @m_c_testgate_delay
                # expensive, dont check too often
                @m_c_testgate_delay -= 1
                return if @m_c_testgate_delay > 0

                # re-dump garbage + smooth reserve accesspath
                construct_cistern(@m_c_reserve)

                gate = df.map_tile_at(*@m_c_reserve.misc[:channel_enable])
                if gate.shape_basic == :Wall
                    ai.debug 'cistern: test channel'
                    empty = true
                    todo = [@m_c_reserve]
                    while empty and r = todo.shift
                        todo.concat r.accesspath
                        empty = false if ((r.x1-1)..(r.x2+1)).any? { |x| ((r.y1-1)..(r.y2+1)).any? { |y| (r.z1..r.z2).any? { |z|
                            next unless t = df.map_tile_at(x, y, z)
                            if not smooth?(t)
                                ai.debug "cistern: unsmoothed #{t.inspect}"
                                true
                            elsif t.occupancy.unit or t.occupancy.unit_grounded
                                ai.debug "cistern: unit #{t.inspect} #{DwarfAI::describe_unit(df.unit_find(t))}"
                                true
                            elsif t.occupancy.item
                                ai.debug "cistern: item #{t.inspect} #{DwarfAI::describe_item(df.item_find(t))}"
                                true
                            else
                                false
                            end
                        } } }
                    end

                    if empty and !dump_items_access(@m_c_reserve)
                        if not f_out_closed
                            # avoid floods. wait for the output to be closed.
                            pull_lever(@m_c_lever_out)
                            @m_c_testgate_delay = 4
                        else
                            pull_lever(@m_c_lever_in) if f_in_closed
                            ai.debug 'cistern: do channel'
                            gate.offset(0, 0, 1).dig(:Channel)
                        end
                    elsif find_room(:well).maptile1.offset(-2, find_room(:well).h/2).designation.flow_size == 7
                        # something went not as planned, but we have a water source
                        @m_c_testgate_delay = nil
                    else
                        # make sure we can actually access the cistern
                        pull_lever(@m_c_lever_in) if f_in_closed
                        pull_lever(@m_c_lever_out) if f_out_closed
                        @m_c_testgate_delay = 16
                    end
                else
                    @m_c_testgate_delay = nil
                    # channel surrounding tiles
                    gate.spiral_search(1, 1) { |tt|
                        tm = tt.tilemat
                        next if tt.shape_basic != :Wall or (tm != :STONE and tm != :MINERAL and tm != :SOIL and tm != :ROOT)
                        if tt.spiral_search(1) { |ttt| ttt.designation.feature_local }
                            tt.offset(0, 0, 1).dig(:Channel)
                        end
                        false
                    }
                end
                return
            end

            return unless @m_c_cistern.misc[:channeled]

            # cistlvl = water level for the top floor
            # if it is flooded, reserve output tile is probably > 4 and prevents buildingdestroyers from messing with the floodgates
            cistlvl = df.map_tile_at(@m_c_cistern).offset(0, 0, 1).designation.flow_size
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

        # returns one tile of an outdoor river (if one exists)
        def scan_river
            ifeat = df.world.features.map_features.find { |f| f.kind_of?(DFHack::FeatureInitOutdoorRiverst) }
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
            ai.debug 'setting up fort blueprint...'
            # TODO place fort body first, have main stair stop before surface, and place trade depot on path to surface
            scan_fort_entrance
            ai.debug 'blueprint found entrance'
            # TODO if no room for fort body, make surface fort
            scan_fort_body
            ai.debug 'blueprint found body'
            setup_blueprint_rooms
            ai.debug 'blueprint found rooms'
            # ensure traps are on the surface
            @fort_entrance.layout.each { |i|
                i[:z] = surface_tile_at(@fort_entrance.x1+i[:x], @fort_entrance.y1+i[:y], true).z-@fort_entrance.z1
            }
            @fort_entrance.layout.delete_if { |i|
                t = df.map_tile_at(@fort_entrance.x1+i[:x], @fort_entrance.y1+i[:y], @fort_entrance.z1+i[:z]-1)
                tm = t.tilemat
                t.shape_basic != :Wall or (tm != :STONE and tm != :MINERAL and tm != :SOIL and tm != :ROOT and (not @allow_ice or tm != :FROZEN_LIQUID))
            }
            list_map_veins
            setup_outdoor_gathering_zones
            setup_blueprint_caverns
            make_map_walkable
            ai.debug 'LET THE GAME BEGIN!'
        end

        def make_map_walkable
            df.onupdate_register_once('df-ai plan make_map_walkable') {
                # if we don't have a river, we're fine
                next true unless river = scan_river
                next true if surface = surface_tile_at(river) and surface.tilemat == :BROOK

                river = spiral_search(river) { |t|
                    # TODO rooms outline
                    (t.y < @fort_entrance.y+MinY or t.y > @fort_entrance.y+MaxY) and
                    t.designation.feature_local
                }
                next true unless river

                # find a safe place for the first tile
                t1 = spiral_search(river) { |t| map_tile_in_rock(t) and st = surface_tile_at(t) and map_tile_in_rock(st.offset(0, 0, -1)) }
                next true unless t1
                t1 = surface_tile_at(t1)

                # if the game hasn't done any pathfinding yet, wait until the next frame and try again
                next false if (t1w = t1.mapblock.walkable[t1.x & 15][t1.y & 15]) == 0

                # find the second tile
                t2 = spiral_search(t1) { |t| tw = t.mapblock.walkable[t.x & 15][t.y & 15] and tw != 0 and tw != t1w }
                next true unless t2
                t2w = t2.mapblock.walkable[t2.x & 15][t2.y & 15]

                # make sure the second tile is in a safe place
                t2 = spiral_search(t2) { |t| t.mapblock.walkable[t.x & 15][t.y & 15] == t2w and map_tile_in_rock(t.offset(0, 0, -1)) }

                # find the bottom of the staircases
                z = df.world.features.map_features.find { |f| f.kind_of?(DFHack::FeatureInitOutdoorRiverst) }.feature.min_map_z.min - 1

                # make the corridors
                cor = []
                cor << Corridor.new(t1.x, t1.x, t1.y, t1.y, t1.z, z)
                cor << Corridor.new(t2.x, t2.x, t2.y, t2.y, t2.z, z)
                cor << Corridor.new(t1.x - (t1.x <=> t2.x), t2.x - (t2.x <=> t1.x), t1.y, t1.y, z, z) if (t1.x - t2.x).abs > 1
                cor << Corridor.new(t2.x, t2.x, t1.y - (t1.y <=> t2.y), t2.y - (t2.y <=> t1.y), z, z) if (t1.y - t2.y).abs > 1
                cor << Corridor.new(t2.x, t2.x, t1.y, t1.y, z, z) if (t1.x - t2.x).abs > 1 and (t1.y - t2.y).abs > 1

                cor.each do |c|
                    @corridors << c
                    wantdig c
                end

                true
            }
        end

        attr_accessor :map_veins

        # scan the map, list all map veins in @map_veins (mat_index => [block coords], sorted by z)
        def list_map_veins
            @map_veins = {}
            i = 0
            df.onupdate_register_once('df-ai plan list_map_veins') {
                if i < df.world.map.z_count
                    df.each_map_block_z(i) { |block|
                        block.block_events.grep(DFHack::BlockSquareEventMineralst).each { |vein|
                            @map_veins[vein.inorganic_mat] ||= []
                            @map_veins[vein.inorganic_mat] << [block.map_pos.x, block.map_pos.y, block.map_pos.z]
                        }
                    }
                    i += 1
                    false
                else
                    @map_veins.each { |mat, list|
                        list.replace list.sort_by { |x, y, z| z + rand(6) }
                    }
                    true
                end
            }
        end

        # mark a vein of a mat for digging, return expected boulder count
        def dig_vein(mat, want_boulders = 1)
            # mat => [x, y, z, dig_mode] marked for to dig
            @map_vein_queue ||= {}
            @dug_veins ||= {}

            count = 0
            # check previously queued veins
            if q = @map_vein_queue[mat]
                q.delete_if { |x, y, z, dd|
                    t = df.map_tile_at(x, y, z)
                    if t.shape_basic == :Open
                        try_furnish_construction(nil, {:construction => dd == :Default ? :Floor : dd}, t)
                    elsif t.shape_basic != :Wall
                        true
                    else
                        t.dig(dd) if t.designation.dig == :No     # warm/wet tile
                        count += 1 if t.tilemat == :MINERAL and t.mat_index_vein == mat
                        false
                    end
                }
                @map_vein_queue.delete mat if q.empty?
            end

            # queue new vein
            # delete it from @map_veins
            # discard tiles that would dig into a @plan room/corridor, or into a cavern (hidden + !wall)
            # try to find a vein close to one we already dug
            16.times {
                break if count/4 >= want_boulders
                @map_veins.delete mat if @map_veins[mat] == []
                break if not @map_veins[mat]
                v = @map_veins[mat].find { |x, y, z|
                    (-1..1).find { |vx| (-1..1).find { |vy| @dug_veins[[x+16*vx, y+16*vy, z]] } }
                } || @map_veins[mat].last
                @map_veins[mat].delete v
                cnt = do_dig_vein(mat, *v)
                @dug_veins[v] = true if cnt > 0
                count += cnt
            }

            count/4
        end

        def do_dig_vein(mat, bx, by, bz)
            ai.debug "dig_vein #{df.world.raws.inorganics[mat].id}"
            count = 0
            fort_minz ||= @corridors.map { |c| c.z1 if c.subtype != :veinshaft }.compact.min
            if bz >= fort_minz
                # TODO rooms outline
                if by > @fort_entrance.y+MinY and by < @fort_entrance.y+MaxY
                    return count
                end
            end

            q = @map_vein_queue[mat] ||= []

            # dig whole block
            # TODO have the dwarves search for the vein
            # TODO mine in (visible?) chunks
            dxs = []
            dys = []
            (0..15).each { |dx| (0..15).each { |dy|
                next if bx+dx == 0 or by+dy == 0 or bx+dx >= df.world.map.x_count-1 or by+dy >= df.world.map.y_count-1
                t = df.map_tile_at(bx+dx, by+dy, bz)
                if t.tilemat == :MINERAL and t.mat_index_vein == mat
                    dxs |= [dx]
                    dys |= [dy]
                end
            } }

            return count if dxs.empty?

            need_shaft = true
            todo = []
            ai.debug "do_dig_vein #{dxs.min}..#{dxs.max} #{dys.min}..#{dys.max}"
            (dxs.min..dxs.max).each { |dx| (dys.min..dys.max).each { |dy|
                t = df.map_tile_at(bx+dx, by+dy, bz)
                if t.designation.dig == :No
                    ok = true
                    ns = need_shaft
                    (-1..1).each { |ddx| (-1..1).each { |ddy|
                        tt = t.offset(ddx, ddy)
                        if tt.shape_basic != :Wall
                            tt.designation.hidden ? ok = false : ns = false
                        elsif tt.designation.dig != :No
                            ns = false
                        end
                    } }
                    if ok
                        todo << [t.x, t.y, t.z, :Default]
                        count += 1 if t.tilemat == :MINERAL and t.mat_index_vein == mat
                        need_shaft = ns
                    end
                end
            } }
            todo.each { |x, y, z, dd|
                q << [x, y, z, dd]
                df.map_tile_at(x, y, z).dig(dd)
            }

            if need_shaft
                # TODO minecarts?

                # avoid giant vertical runs: slalom x+-1 every z%16

                if by > @fort_entrance.y
                    vert = df.map_tile_at(@fort_entrance.x, @fort_entrance.y+30, bz)   # XXX 30...
                else
                    vert = df.map_tile_at(@fort_entrance.x, @fort_entrance.y-30, bz)
                end
                vert = vert.offset(1, 0) if (vert.z % 32) > 16  # XXX check this

                t0 = df.map_tile_at(bx+(dxs.min+dxs.max)/2, by+(dys.min+dys.max)/2, bz)
                while t0.y != vert.y
                    t0.dig(:Default)
                    q << [t0.x, t0.y, t0.z, :Default]
                    t0 = t0.offset(0, (t0.y > vert.y ? -1 : 1))
                end
                while t0.x != vert.x
                    t0.dig(:Default)
                    q << [t0.x, t0.y, t0.z, :Default]
                    t0 = t0.offset((t0.x > vert.x ? -1 : 1), 0)
                end
                while t0.designation.hidden
                    if t0.z % 16 == 0
                        t0.dig(:DownStair)
                        q << [t0.x, t0.y, t0.z, :DownStair]
                        t0 = t0.offset(((t0.z % 32) >= 16 ? 1 : -1), 0)
                        t0.dig(:UpStair)
                        q << [t0.x, t0.y, t0.z, :UpStair]
                    else
                        t0.dig(:UpDownStair)
                        q << [t0.x, t0.y, t0.z, :UpDownStair]
                    end
                    break if not t0 = t0.offset(0, 0, 1)
                end
                if t0 and not t0.designation.hidden and not t0.shape_passablelow and t0.designation.dig == :No
                    t0.dig(:DownStair)
                    q << [t0.x, t0.y, t0.z, :DownStair]
                end
            end

            count
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

        MinX, MinY, MinZ = -48, -22, -5
        MaxX, MaxY, MaxZ = 35, 22, 1

        # search a valid tile for fortress entrance
        def scan_fort_entrance
            # map center
            cx = df.world.map.x_count / 2
            cy = df.world.map.y_count / 2
            center = surface_tile_at(cx, cy, true)
            rangez = (0...df.world.map.z_count).to_a.reverse
            cz = rangez.find { |z| t = df.map_tile_at(cx, cy, z) and tsb = t.shape_basic and (tsb == :Floor or tsb == :Ramp) }
            center = df.map_tile_at(cx, cy, cz)

            ent0 = center.spiral_search { |t0|
                # test the whole map for 3x5 clear spots
                next unless t = surface_tile_at(t0)

                # make sure we're not too close to the edge of the map.
                next unless t.offset(MinX, MinY, MinZ) and t.offset(MaxX, MaxY, MaxZ)

                (-1..1).all? { |_x|
                    (-2..2).all? { |_y|
                        tt = t.offset(_x, _y, -1) and tt.shape == :WALL and tm = tt.tilemat and (tm == :STONE or tm == :MINERAL or tm == :SOIL or tm == :ROOT) and
                        ttt = t.offset(_x, _y) and ttt.shape == :FLOOR and ttt.designation.flow_size == 0 and
                         not ttt.designation.hidden and not df.building_find(ttt)
                    }
                } and (-3..3).all? { |_x|
                    (-4..4).all? { |_y|
                        surface_tile_at(t.x + _x, t.y + _y, true)
                    }
                }
            }

            if not ent0
                @allow_ice = true

                ent0 = center.spiral_search { |t0|
                    # test the whole map for 3x5 clear spots
                    next unless t = surface_tile_at(t0)

                    # make sure we're not too close to the edge of the map.
                    next unless t.offset(MinX, MinY, MinZ) and t.offset(MaxX, MaxY, MaxZ)

                    (-1..1).all? { |_x|
                        (-2..2).all? { |_y|
                            tt = t.offset(_x, _y, -1) and tt.shape == :WALL and
                            ttt = t.offset(_x, _y) and ttt.shape == :FLOOR and ttt.designation.flow_size == 0 and
                             not ttt.designation.hidden and not df.building_find(ttt)
                        }
                    } and (-3..3).all? { |_x|
                        (-4..4).all? { |_y|
                            surface_tile_at(t.x + _x, t.y + _y, true)
                        }
                    }
                }
            end

            raise 'Can\'t find a fortress entrance spot. We need a 3x5 flat area with solid ground for at least 2 tiles on each side.' unless ent0
            ent = surface_tile_at(ent0)

            @fort_entrance = Corridor.new(ent.x, ent.x, ent.y-1, ent.y+1, ent.z, ent.z)
            3.times { |i|
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => i-1, :y => -1,  :ignore => true}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 1,   :y => i,   :ignore => true}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 1-i, :y => 3,   :ignore => true}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => -1,  :y => 2-i, :ignore => true}
            }
            5.times { |i|
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => i-2, :y => -2,  :ignore => true}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 2,   :y => i-1, :ignore => true}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 2-i, :y => 4,   :ignore => true}
                @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => -2,  :y => 3-i, :ignore => true}
            }

        end

        # search how much we need to dig to find a spot for the full fortress body
        # here we cheat and work as if the map was fully reveal()ed
        def scan_fort_body
            # use a hardcoded fort layout
            cx, cy, cz = @fort_entrance.x, @fort_entrance.y, @fort_entrance.z

            @fort_entrance.z1 = (0..cz).to_a.reverse.find { |cz1|
                # stop searching if we hit a cavern or an aquifer inside our main staircase
                break unless (0..0).all? { |dx|
                    (-1..1).all? { |dy|
                        t = df.map_tile_at(cx+dx, cy+dy, cz1+MaxZ) and
                        map_tile_nocavern(t) and
                        not t.designation.water_table
                    }
                }

                (MinZ..MaxZ).all? { |dz|
                    # scan perimeter first to quickly eliminate caverns / bad rock layers
                    (MinX..MaxX).all? { |dx|
                        [MinY, MaxY].all? { |dy|
                            t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                            not t.designation.water_table and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or (@allow_ice and tm == :FROZEN_LIQUID) or (dz > -1 and (tm == :SOIL or tm == :ROOT)))
                        }
                    } and
                    [MinX, MaxX].all? { |dx|
                        (MinY..MaxY).all? { |dy|
                            t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                            not t.designation.water_table and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or (@allow_ice and tm == :FROZEN_LIQUID) or (dz > -1 and (tm == :SOIL or tm == :ROOT)))
                        }
                    }
                }  and
                # perimeter ok, full scan
                (MinZ..MaxZ).all? { |dz|
                    ((MinX+1)..(MaxX-1)).all? { |dx|
                        ((MinY+1)..(MaxY-1)).all? { |dy|
                            t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                            not t.designation.water_table and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or (@allow_ice and tm == :FROZEN_LIQUID) or (dz > -1 and (tm == :SOIL or tm == :ROOT)))
                        }
                    }
                }
            }

            raise 'Too many caverns, cant find room for fort. We need more minerals!' unless @fort_entrance.z1
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

            # Millstone, Siege, magma workshops/furnaces
            types = [:Still,:Kitchen, :Fishery,:Butchers, :Leatherworks,:Tanners,
                :Loom,:Clothiers, :Dyers,:Bowyers, nil,:Kiln]
            types += [:Masons,:Carpenters, :Mechanics,:Farmers, :Craftsdwarfs,:Jewelers,
                :Smelter,:MetalsmithsForge, :Ashery,:WoodFurnace, :SoapMaker,:GlassFurnace]

            [-1, 1].each { |dirx|
                prev_corx = (dirx < 0 ? corridor_center0 : corridor_center2)
                ocx = fx + dirx*3
                (1..6).each { |dx|
                    # segments of the big central horizontal corridor
                    cx = fx + dirx*4*dx
                    cor_x = Corridor.new(ocx, cx + (dx <= 5 ? 0 : dirx), fy-1, fy+1, fz, fz)
                    cor_x.accesspath = [prev_corx]
                    @corridors << cor_x
                    prev_corx = cor_x
                    ocx = cx+dirx

                    if dirx == 1 and dx == 3
                        # stuff a quern&screwpress near the farmers'
                        @rooms << Room.new(:workshop, :Quern, cx-2, cx-2, fy+1, fy+1, fz)
                        @rooms.last.accesspath = [cor_x]
                        @rooms.last.misc[:workshop_level] = 0

                        @rooms << Room.new(:workshop, :Quern, cx-6, cx-6, fy+1, fy+1, fz)
                        @rooms.last.accesspath = [cor_x]
                        @rooms.last.misc[:workshop_level] = 1

                        @rooms << Room.new(:workshop, :Quern, cx+2, cx+2, fy+1, fy+1, fz)
                        @rooms.last.accesspath = [cor_x]
                        @rooms.last.misc[:workshop_level] = 2

                        @rooms << Room.new(:workshop, :ScrewPress, cx-2, cx-2, fy-1, fy-1, fz)
                        @rooms.last.accesspath = [cor_x]
                        @rooms.last.misc[:workshop_level] = 0
                    end

                    t = types.shift
                    @rooms << Room.new(:workshop, t, cx-1, cx+1, fy-5, fy-3, fz)
                    @rooms.last.accesspath = [cor_x]
                    @rooms.last.layout << {:item => :door, :x => 1, :y => 3}
                    @rooms.last.misc[:workshop_level] = 0
                    if dirx == -1 and dx == 1
                        @rooms.last.layout << {:item => :nestbox, :x => -1, :y => 4}
                    end

                    @rooms << Room.new(:workshop, t, cx-1, cx+1, fy-8, fy-6, fz)
                    @rooms.last.accesspath = [@rooms[@rooms.length - 2]]
                    @rooms.last.misc[:workshop_level] = 1

                    @rooms << Room.new(:workshop, t, cx-1, cx+1, fy-11, fy-9, fz)
                    @rooms.last.accesspath = [@rooms[@rooms.length - 2]]
                    @rooms.last.misc[:workshop_level] = 2

                    t = types.shift
                    @rooms << Room.new(:workshop, t, cx-1, cx+1, fy+3, fy+5, fz)
                    @rooms.last.accesspath = [cor_x]
                    @rooms.last.layout << {:item => :door, :x => 1, :y => -1}
                    @rooms.last.misc[:workshop_level] = 0
                    if dirx == -1 and dx == 1
                        @rooms.last.layout << {:item => :nestbox, :x => -1, :y => -2}
                    end

                    @rooms << Room.new(:workshop, t, cx-1, cx+1, fy+6, fy+8, fz)
                    @rooms.last.accesspath = [@rooms[@rooms.length - 2]]
                    @rooms.last.misc[:workshop_level] = 1

                    @rooms << Room.new(:workshop, t, cx-1, cx+1, fy+9, fy+11, fz)
                    @rooms.last.accesspath = [@rooms[@rooms.length - 2]]
                    @rooms.last.misc[:workshop_level] = 2
                }
            }

            depot_center = spiral_search(df.map_tile_at(@fort_entrance.x - 4, @fort_entrance.y, @fort_entrance.z2 - 1)) { |t|
                (-2..2).all? { |dx| (-2..2).all? { |dy|
                    tt = t.offset(dx, dy, 0) and
                    map_tile_in_rock(tt) and
                    not map_tile_intersects_room(tt)
                } } and (-1..1).all? { |dy|
                    tt = t.offset(-3, dy, 0) and
                    map_tile_in_rock(tt) and
                    ttt = tt.offset(0, 0, 1) and
                    ttt.shape_basic == :Floor and
                    not map_tile_intersects_room(tt) and
                    not map_tile_intersects_room(ttt)
                }
            }

            if depot_center
                r = Room.new(:workshop, :TradeDepot, depot_center.x-2, depot_center.x+2, depot_center.y-2, depot_center.y+2, depot_center.z)
                r.misc[:workshop_level] = 0
                r.layout << { :dig => :Ramp, :x => -1, :y => 1 }
                r.layout << { :dig => :Ramp, :x => -1, :y => 2 }
                r.layout << { :dig => :Ramp, :x => -1, :y => 3 }
                @rooms << r
            else
                r = Room.new(:workshop, :TradeDepot, @fort_entrance.x-7, @fort_entrance.x-3, @fort_entrance.y-2, @fort_entrance.y+2, @fort_entrance.z2)
                r.misc[:workshop_level] = 0
                5.times { |x| 5.times { |y|
                    r.layout << { :construction => :Floor, :x => x, :y => y }
                } }
                @rooms << r
            end
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

            types = [:food,:furniture, :wood,:stone, :refuse,:animals, :corpses,:gems]
            types += [:finished_goods,:cloth, :bars_blocks,:leather, :ammo,:armor, :weapons,:coins]

            # TODO side stairs to workshop level ?
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
                    @rooms[-2, 2].each { |r| r.misc[:stockpile_level] = 1 }
                    @rooms[-2, 2].each { |r|
                        r.accesspath = [cor_x]
                        r.layout << {:item => :door, :x => 2, :y => (r.y < fy ? 2 : -1)}
                        r.layout << {:item => :door, :x => 4, :y => (r.y < fy ? 2 : -1)}
                    }
                    @rooms << Room.new(:stockpile, t0, cx-3, cx+3, fy-5, fy-11, fz)
                    @rooms << Room.new(:stockpile, t1, cx-3, cx+3, fy+5, fy+11, fz)
                    @rooms[-2, 2].each { |r| r.misc[:stockpile_level] = 2 }
                    @rooms << Room.new(:stockpile, t0, cx-3, cx+3, fy-12, fy-20, fz)
                    @rooms << Room.new(:stockpile, t1, cx-3, cx+3, fy+12, fy+20, fz)
                    @rooms[-2, 2].each { |r| r.misc[:stockpile_level] = 3 }
                }
            }
            @rooms.each { |r|
                if r.type == :stockpile and r.subtype == :coins and r.misc[:stockpile_level] > 1
                    r.subtype = :furniture
                    r.misc[:stockpile_level] += 2
                end
            }

            setup_blueprint_minecarts
            setup_blueprint_pitcage
        end

        def setup_blueprint_minecarts
            return # disabled 2015-02-04 BenLubar

            last_stone = @rooms.find_all { |r| r.type == :stockpile and r.subtype == :stone }.last
            return if not last_stone

            rx = last_stone.x
            ry = last_stone.y + (last_stone.y > @fort_entrance.y ? 6 : -6)
            rz = last_stone.z
            dirx = (rx > @fort_entrance.x ? 1 : -1)
            diry = (ry > @fort_entrance.y ? 1 : -1)
            x1 = (dirx > 0 ? 1 : 0)
            y1 = (diry > 0 ? 1 : 0)

            # minecart dumping microstockpile
            r = Room.new(:stockpile, :stone, rx, rx, ry, ry+diry, rz)
            r.layout << { :item => :door, :x => 0, :y => (diry > 0 ? -1 : 2) }
            r.misc[:stockpile_level] = last_stone.misc[:stockpile_level] + 1
            r.accesspath = [last_stone]
            @rooms << r

            # cartway: dumping side (end of jump, trackstop dump, fall into channel to reset path
            r1 = Room.new(:cartway, :fort, rx-dirx, rx-2*dirx, ry, ry+diry, rz)
            r1.layout << { :construction => :Track, :x =>   x1, :y =>   y1, :dir => [:n, :s], :dig => :Ramp }
            r1.layout << { :construction => :Track, :x =>   x1, :y => 1-y1, :dir => [(dirx > 0 ? :w : :e), (diry > 0 ? :s : :n)] }
            r1.layout << { :construction => :Track, :x => 1-x1, :y => 1-y1, :dir => [(dirx > 0 ? :e : :w), (diry > 0 ? :s : :n)] }
            r1.layout << { :construction => :Track, :x => 1-x1, :y =>   y1, :dir => [:n, :s] }
            r1.layout << { :item => :trap, :subtype => :trackstop, :x => x1, :y => 1-y1,
                :misc_bldprops => { :friction => 10, :dump_x_shift => dirx, :dump_y_shift => 0, :use_dump => 1 } }
            r1.accesspath = [@rooms.last]

            ry += 2*diry
            ey = (diry > 0 ? (2*df.world.map.y_count+ry)/3 : ry/3)

            corr = Corridor.new(rx, rx, ry+3*diry, ry+3*diry, rz-1, rz)
            corr.z2 += 1 while map_tile_in_rock(corr.maptile2)
            @corridors << corr

            # power shaft + windmill to power the roller
            rshaft = Room.new(:cartway, :shaft, rx, rx, ry+4*diry, ry+5*diry, rz)
            rshaft.accesspath = [corr]
            rshaft.z2 += 1 while map_tile_in_rock(rshaft.maptile2)
            rshaft.layout << { :item => :gear_assembly, :x => 0, :y => (diry > 0 ? 1 : 0), :z => 0 }
            rshaft.layout << { :item => :roller, :x => -dirx,    :y => (diry > 0 ? 1 : 0), :z => 0,
                    :misc_bldprops => {:speed => 50000, :direction => (diry > 0 ? :FromSouth : :FromNorth)} }
            (1...rshaft.h_z-1).each { |i|
                rshaft.layout << { :item => :vertical_axle, :x => 0, :y => (diry > 0 ? 1 : 0), :z => i, :dig => :Channel }
            }
            rshaft.layout << { :item => :windmill, :x => 0, :y => (diry > 0 ? 1 : 0), :z => rshaft.h_z-1, :dig => :Channel }

            # main 'outside' room with minecart jump (ramp), air path, and way back on track (from trackstop channel)
            # 2xYx2 dimensions + cheat with layout
            # fitting tightly within r1+rshaft
            # use :dig => :Ramp hack to channel z+1
            r2 = Room.new(:cartway, :out, rx-dirx, rx-2*dirx, ry, ey, rz-1)
            r2.z2 += 1
            r2.accesspath = [corr]
            dx0 = (dirx>0 ? 0 : 1)
            # part near fort
            # air gap
            r2.layout << { :construction => :NoRamp, :x =>   dx0, :y => (diry > 0 ? 0 : r2.h-1 ), :z => 0, :dig => :Ramp }
            r2.layout << { :construction => :NoRamp, :x => 1-dx0, :y => (diry > 0 ? 0 : r2.h-1 ), :z => 0, :dig => :Ramp }
            # z-1: channel fall reception
            (1..4).each { |i|
                r2.layout << { :construction => :Track,  :x =>   dx0, :y => (diry > 0 ? i : r2.h-1-i ), :z => 0, :dig => :Ramp, :dir => [:n, :s] }
                r2.layout << { :construction => :NoRamp, :x => 1-dx0, :y => (diry > 0 ? i : r2.h-1-i ), :z => 0, :dig => :Ramp } if i < 3
            }
            # z: jump
            r2.layout << { :construction => :Wall, :x => 1-dx0, :y => (diry > 0 ? 3 : r2.h-4), :z => 1, :dig => :No }
            r2.layout << { :construction => :TrackRamp, :x => 1-dx0, :y => (diry > 0 ? 4 : r2.h-5), :z => 1, :dir => [:n, :s], :dig => :Ramp }
            # z+1: airway
            r2.layout << { :x => 1-dx0, :y => (diry > 0 ? 0 : r2.h-1), :z => 2, :dig => :Channel }
            r2.layout << { :x => 1-dx0, :y => (diry > 0 ? 1 : r2.h-2), :z => 2, :dig => :Channel }
            r2.layout << { :x => 1-dx0, :y => (diry > 0 ? 2 : r2.h-3), :z => 2, :dig => :Channel }
            r2.layout << { :x => (dirx>0 ? 2 : -1), :y => (diry > 0 ? 1 : r2.h-2), :z => 2 } # access to dig preceding Channels from rshaft
            r2.layout << { :x => (dirx>0 ? 2 : -1), :y => (diry > 0 ? 2 : r2.h-3), :z => 2 }
            r2.layout << { :construction => :Track, :x => 1-dx0, :y => (diry > 0 ? 3 : r2.h-4), :z => 2, :dir => [:n, :s] }
            # main track
            (5..(r2.h-2)).each { |i|
                r2.layout << { :construction => :Track, :x =>   dx0, :y => (diry > 0 ? i : r2.h-1-i), :z => 0, :dir => [:n, :s] }
                r2.layout << { :construction => :Track, :x => 1-dx0, :y => (diry > 0 ? i : r2.h-1-i), :z => 1, :dir => [:n, :s] }
            }
            # track termination + manual reset ramp
            r2.layout << { :construction => :Ramp, :x =>   dx0, :y => (diry > 0 ? r2.h-1 : 0), :z => 0, :dig => :Ramp }
            r2.layout << { :construction => :Wall, :x => 1-dx0, :y => (diry > 0 ? r2.h-1 : 0), :z => 0, :dig => :No }
            r2.layout << { :construction => :Track, :x => 1-dx0, :y => (diry > 0 ? r2.h-1 : 0), :z => 1, :dir => [(diry > 0 ? :n : :s)] }

            # order matters to prevent unauthorized access to fortress (eg temporary ramps)
            @rooms << rshaft << r2 << r1

            corr = Corridor.new(rx-dirx, rx-dirx, ey+4*diry, ey+4*diry, rz, rz)
            corr.z2 += 1 while map_tile_in_rock(corr.maptile2)
            @corridors << corr
            st = Room.new(:stockpile, :stone, rx-dirx, rx-dirx, ey+diry, ey+3*diry, rz)
            st.misc[:stockpile_level] = 100
            st.layout << { :item => :minecart_route, :x => 0, :y => (diry > 0 ? -1 : 3), :direction => (diry > 0 ? :North : :South) }
            st.accesspath = [corr]
            @rooms << st
            [1, -1, 2, -2].each { |dx|
                st = Room.new(:stockpile, :stone, rx-dirx+dx, rx-dirx+dx, ey+diry, ey+3*diry, rz)
                st.misc[:stockpile_level] = 101
                st.accesspath = [@rooms.last]
                @rooms << st
            }
        end

        def setup_blueprint_pitcage
            return if not gpit = find_room(:garbagepit)
            r = Room.new(:pitcage, nil, gpit.x1-1, gpit.x1+1, gpit.y1-1, gpit.y1+1, gpit.z1+10)
            r.layout << { :construction => :UpStair, :x => -1, :y => 1, :z => -10 }
            r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -9 }
            r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -8 }
            r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -7 }
            r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -6 }
            r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -5 }
            r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -4 }
            r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -3 }
            r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -2 }
            r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -1 }
            r.layout << { :construction => :DownStair, :x => -1, :y => 1, :z => 0 }
            [0, 1, 2].each { |dx| [1, 0, 2].each { |dy|
                if dx == 1 and dy == 1
                    r.layout << { :dig => :Channel, :x => dx, :y => dy }
                else
                    r.layout << { :construction => :Floor, :x => dx, :y => dy }
                end
            } }
            r.layout << { :construction => :Floor, :x => 3, :y => 1, :item => :hive }
            @rooms << r
            stockpile = Room.new(:stockpile, :animals, r.x1, r.x2, r.y1, r.y2, r.z1)
            stockpile.misc[:stockpile_level] = 0
            stockpile.layout = r.layout
            @rooms << stockpile
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
                    dinner.layout << {:construction => :Wall, :dig => :No, :x => 2, :y => 3}
                    dinner.layout << {:construction => :Wall, :dig => :No, :x => 8, :y => 3}
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

            if @allow_ice
                ai.debug "icy embark, no well"
            elsif river = scan_river
                setup_blueprint_cistern_fromsource(river, fx, fy, fz)
            else
                # TODO pool, pumps, etc
                ai.debug "no river, no well"
            end

            # farm plots
            farm_h = 3
            farm_w = 3
            dpf = (@dwarves_per_farmtile * farm_h * farm_w).to_i
            nrfarms = (220+dpf-1)/dpf

            cx = fx+4*6       # end of workshop corridor (last ws door)
            cy = fy
            cz = find_room(:workshop) { |r| r.subtype == :Farmers }.z1
            ws_cor = Corridor.new(fx+3, cx+1, cy, cy, cz, cz)    # ws_corr.access_path ...
            @corridors << ws_cor
            farm_stairs = Corridor.new(cx+2, cx+2, cy, cy, cz, cz)
            farm_stairs.accesspath = [ws_cor]
            @corridors << farm_stairs
            cx += 3
            soilcnt = {}
            (cz...df.world.map.z_count).each { |z|
                scnt = 0
                if (-1..(nrfarms*farm_w/3)).all? { |dx|
                    (-(3*farm_h+farm_h-1)..(3*farm_h+farm_h-1)).all? { |dy|
                        t = df.map_tile_at(cx+dx, cy+dy, z)
                        next if not t or t.shape != :WALL
                        scnt += 1 if t.tilemat == :SOIL
                        true
                    }
                }
                    soilcnt[z] = scnt
                end
            }
            cz2 = soilcnt.index(soilcnt.values.max) || cz

            farm_stairs.z2 = cz2
            cor = Corridor.new(cx, cx+1, cy, cy, cz2, cz2)
            cor.accesspath = [farm_stairs]
            @corridors << cor
            types = [:food, :cloth]
            [-1, 1].each { |dy|
                st = types.shift
                (nrfarms/3).times { |dx|
                    3.times { |ddy|
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
            r.misc[:stockpile_level] = 0
            r.misc[:workshop] = @rooms[-2*nrfarms]
            r.accesspath = [cor]
            @rooms << r

            # garbage dump
            # TODO ensure flat space, no pools/tree, ...
            r = Room.new(:garbagedump, nil, cx+5, cx+5, cy, cy, cz)
            tile = spiral_search(df.map_tile_at(r)) { |t|
                t = surface_tile_at(t) and
                (t.x >= cx + 5 or
                (t.z > cz + 2 and t.x > fx + 5)) and
                map_tile_in_rock(t.offset(0, 0, -1)) and
                map_tile_in_rock(t.offset(2, 0, -1)) and
                t.shape_basic == :Floor and
                t.offset(1, 0, 0).shape_basic == :Floor and
                t.offset(2, 0, 0).shape_basic == :Floor and
                (t.offset(1, 1, 0).shape_basic == :Floor or
                t.offset(1, -1, 0).shape_basic == :Floor)
            }
            r.x1 = r.x2 = tile.x
            r.y1 = r.y2 = tile.y
            r.z2 = r.z1 = surface_tile_at(tile).z
            @rooms << r
            r = Room.new(:garbagepit, nil, @rooms.last.x + 1, @rooms.last.x + 2, @rooms.last.y, @rooms.last.y, @rooms.last.z)
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

            500.times { |ry|
                break if (-1..19).find { |tx| (-1..4).find { |ty|
                    not t = df.map_tile_at(fx+10+tx, fy-3-3*ry-ty, fz) or t.shape_basic != :Wall
                } }
                2.times { |rrx|
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
                    barracks.layout << {:item => :archerytarget, :x => 3, :y => (ry>0 ? 7 : 0), :ignore => true, :users => []}
                    barracks.accesspath = [cor]
                    @rooms << barracks
                }
            }

            setup_blueprint_pastures
            setup_blueprint_outdoor_farms(nrfarms * 2)
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
            well.layout << {:item => :well, :x => 4, :y => 4, :makeroom => true, :dig => :Channel}
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
            reserve.layout << {:item => :floodgate, :x =>  5, :y => 3, :way => :out, :ignore => true}
            reserve.accesspath = [cist_cors[0]]

            # cisterns are dug in order
            # try to dig reserve first, so we have liquid water even if river freezes
            @rooms << reserve
            @rooms << cistern

            # link the cistern reserve to the water source

            # trivial walk of the river tiles to find a spot closer to dst
            move_river = lambda { |dst|
                nsrc = src
                500.times {
                    break if not nsrc
                    src = nsrc
                    dist = src.distance_to(dst)
                    nsrc = spiral_search(src, 1, 1) { |t|
                        next if t.distance_to(dst) > dist
                        t.designation.feature_local
                    }
                }
            }

            # 1st end: reservoir input
            p1 = df.map_tile_at(cx-16, fy, fz)
            move_river[p1]
            ai.debug "cistern: reserve/in (#{p1.x}, #{p1.y}, #{p1.z}), river (#{src.x}, #{src.y}, #{src.z})"

            # XXX hardcoded layout again
            if src.x <= p1.x
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

            dst = up[-1].maptile2.offset(0, 0, -2)
            dst = df.map_tile_at(dst.x, dst.y, src.z - 1) if src.z <= dst.z
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
            ai.debug "cistern: channel_enable (#{channel.x}, #{channel.y}, #{channel.z})" if channel

            # TODO check that 'channel' is easily channelable (eg river in a hole)

            if (dst.x - out.x).abs > 1
                cor = Corridor.new(dst.x - (dst.x <=> out.x), out.x - (out.x <=> dst.x), dst.y, dst.y, dst.z, dst.z)
                @corridors << cor
                r.accesspath << cor
                r = cor
            end

            if (dst.y - out.y).abs > 1
                cor = Corridor.new(out.x - (out.x <=> dst.x), out.x - (out.x <=> dst.x), dst.y + (out.y <=> dst.y), out.y, dst.z, dst.z)
                @corridors << cor
                r.accesspath << cor
            end

            up = find_corridor_tosurface(df.map_tile_at(out.x, out.y, dst.z))
            r.accesspath << up[0]

            reserve.misc[:channel_enable] = [channel.x, channel.y, channel.z] if channel
        end

        # scan for 11x11 flat areas with grass
        def setup_blueprint_pastures
            want = 36
            @fort_entrance.maptile.spiral_search([df.world.map.x_count, df.world.map.y_count].max, 12, 12) { |_t|
                next unless sf = surface_tile_at(_t)
                grasstile = 0
                if (-5..5).all? { |dx| (-5..5).all? { |dy|
                    if tt = sf.offset(dx, dy) and (tt.shape_basic == :Floor or tt.tilemat == :TREE) and
                            tt.designation.flow_size == 0 and tt.tilemat != :FROZEN_LIQUID
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

        # scan for 3x3 flat areas with soil
        def setup_blueprint_outdoor_farms(want)
            @fort_entrance.maptile.spiral_search([df.world.map.x_count, df.world.map.y_count].max, 9, 3) { |_t|
                next unless sf = surface_tile_at(_t)
                sd = sf.designation
                if (-1..1).all? { |dx| (-1..1).all? { |dy|
                    tt = sf.offset(dx, dy) and
                    td = tt.designation and
                    ((sd.subterranean and td.subterranean) or
                    (not sd.subterranean and not td.subterranean and
                        sd.biome == td.biome)) and
                    tt.shape_basic == :Floor and
                    tt.designation.flow_size == 0 and
                    [:GRASS_DARK, :GRASS_LIGHT, :SOIL].include?(tt.tilemat)
                } }
                    @rooms << Room.new(:farmplot, [:food, :cloth][want % 2], sf.x-1, sf.x+1, sf.y-1, sf.y+1, sf.z)
                    @rooms.last.misc[:users] = []
                    @rooms.last.misc[:outdoor] = true
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
                cor_x2 = Corridor.new(ocx-dirx, fx+dirx*3, fy, fy, fz, fz)
                cor_x.accesspath = [cor_x2]
                cor_x2.accesspath = [dirx < 0 ? corridor_center0 : corridor_center2]
                @corridors << cor_x << cor_x2

                [-1, 1].each { |diry|
                    @noblesuite ||= -1
                    @noblesuite += 1

                    r = Room.new(:nobleroom, :office, cx-1, cx+1, fy+diry*3, fy+diry*5, fz)
                    r.misc[:noblesuite] = @noblesuite
                    r.accesspath = [cor_x]
                    r.layout << {:item => :chair, :x => 1, :y => 1, :makeroom => true}
                    r.layout << {:item => :chair, :x => 1-dirx, :y => 1}
                    r.layout << {:item => :chest, :x => 1+dirx, :y => 0, :ignore => true}
                    r.layout << {:item => :cabinet, :x => 1+dirx, :y => 2, :ignore => true}
                    r.layout << {:item => :door, :x => 1, :y => 1-2*diry}
                    @rooms << r

                    r = Room.new(:nobleroom, :bedroom, cx-1, cx+1, fy+diry*7, fy+diry*9, fz)
                    r.misc[:noblesuite] = @noblesuite
                    r.layout << {:item => :bed, :x => 1, :y => 1, :makeroom => true}
                    r.layout << {:item => :armorstand, :x => 1-dirx, :y => 0, :ignore => true}
                    r.layout << {:item => :weaponrack, :x => 1-dirx, :y => 2, :ignore => true}
                    r.layout << {:item => :door, :x => 1, :y => 1-2*diry, :internal => true}
                    r.accesspath = [@rooms.last]
                    @rooms << r

                    r = Room.new(:nobleroom, :diningroom, cx-1, cx+1, fy+diry*11, fy+diry*13, fz)
                    r.misc[:noblesuite] = @noblesuite
                    r.layout << {:item => :table, :x => 1, :y => 1, :makeroom => true}
                    r.layout << {:item => :chair, :x => 1+dirx, :y => 1}
                    r.layout << {:item => :cabinet, :x => 1-dirx, :y => 0, :ignore => true}
                    r.layout << {:item => :chest, :x => 1-dirx, :y => 2, :ignore => true}
                    r.layout << {:item => :door, :x => 1, :y => 1-2*diry, :internal => true}
                    r.accesspath = [@rooms.last]
                    @rooms << r

                    r = Room.new(:nobleroom, :tomb, cx-1, cx+1, fy+diry*15, fy+diry*17, fz)
                    r.misc[:noblesuite] = @noblesuite
                    r.layout << {:item => :coffin, :x => 1, :y => 1, :makeroom => true}
                    r.layout << {:item => :door, :x => 1, :y => 1-2*diry, :internal => true}
                    r.accesspath = [@rooms.last]
                    @rooms << r
                }
            }
        end

        def setup_outdoor_gathering_zones
            x = 0
            y = 0
            i = 0
            ground = {}
            bg = df.onupdate_register('df-ai plan setup_outdoor_gathering_zones', 10) do
                bg.description = "df-ai plan setup_outdoor_gathering_zones #{i}"
                if x+i == [x+31, df.world.map.x_count].min
                    ground.keys.each do |tz|
                        g = ground[tz]
                        bld = df.building_alloc(:Civzone, :ActivityZone)
                        bld.zone_flags.active = true
                        bld.zone_flags.gather = true
                        bld.gather_flags.pick_trees = true
                        bld.gather_flags.pick_shrubs = true
                        bld.gather_flags.gather_fallen = true
                        w = 31
                        h = 31
                        w = df.world.map.x_count % 31 if x + 31 > df.world.map.x_count
                        h = df.world.map.y_count % 31 if y + 31 > df.world.map.y_count
                        df.building_position(bld, [x, y, tz], w, h)
                        bld.room.extents = df.malloc(w * h)
                        bld.room.x = x
                        bld.room.y = y
                        bld.room.width = w
                        bld.room.height = h
                        w.times do |cx|
                            h.times do |cy|
                                bld.room.extents[cx + w * cy] = g[[cx, cy]] ? 1 : 0
                            end
                        end
                        bld.is_room = 1
                        df.building_construct_abstract(bld)
                    end

                    ground.clear
                    i = 0
                    x += 31
                    if x >= df.world.map.x_count
                        x = 0
                        y += 31
                        if y >= df.world.map.y_count
                            df.onupdate_unregister(bg)
                            ai.debug 'plan setup_outdoor_gathering_zones finished'
                            next
                        end
                    end
                    next
                end

                tx = x + i
                (y...([y+31, df.world.map.y_count].min)).each do |ty|
                    next unless t = surface_tile_at(tx, ty, true)
                    tz = t.z
                    ground[tz] ||= {}
                    ground[tz][[tx % 31, ty % 31]] = true
                end
                i += 1
            end
        end

        def setup_blueprint_caverns
            wall = nil
            unless (0...(@cavern_max_level ||= df.world.map.z_count)).to_a.reverse.any? { |z|
                ai.debug "outpost: searching z-level #{z}"
                (0...df.world.map.x_count).any? { |x|
                    (0...df.world.map.y_count).any? { |y|
                        t = df.map_tile_at(x, y, z) and
                        map_tile_in_rock(t) and
                        spiral_search(t, 2) { |_t|
                            map_tile_cavernfloor(_t)
                        } and
                        #@rooms.all? { |r|
                        #    not r.safe_include?(t.x, t.y, r.z)
                        #} and @corridors.all? { |r|
                        #    not r.safe_include?(t.x, t.y, r.z)
                        #} and
                        wall = t
                    }
                }
            }
                ai.debug 'outpost: could not find a cavern wall tile'
                @cavern_max_level = 0
                return
            end

            @cavern_max_level = wall.z + 1

            # find a floor next to the wall
            unless target = spiral_search(wall, 2) { |t|
                map_tile_cavernfloor(t)
            }
                ai.debug 'outpost: could not find a cavern floor tile'
                @cavern_max_level = 0
                return
            end

            r = Room.new(:outpost, :cavern, target.x, target.x, target.y, target.y, target.z)

            if (wall.x - target.x).abs > 1
                cor = Corridor.new(wall.x - (wall.x <=> target.x), target.x - (target.x <=> wall.x), wall.y, wall.y, wall.z, wall.z)
                @corridors << cor
                r.accesspath << cor
            end

            if (wall.y - target.y).abs > 1
                cor = Corridor.new(target.x - (target.x <=> wall.x), target.x - (target.x <=> wall.x), wall.y + (target.y <=> wall.y), target.y, wall.z, wall.z)
                @corridors << cor
                r.accesspath << cor
            end

            up = find_corridor_tosurface(wall)
            r.accesspath << up[0]

            ai.debug "outpost: wall (#{wall.x}, #{wall.y}, #{wall.z})"
            ai.debug "outpost: target (#{target.x}, #{target.y}, #{target.z})"
            ai.debug "outpost: up (#{up.last.x2}, #{up.last.y2}, #{up.last.z2})"

            @rooms << r

            true
        end

        # check that tile is surrounded by solid rock/soil walls
        def map_tile_in_rock(tile)
            tile and (-1..1).all? { |dx| (-1..1).all? { |dy|
                t = tile.offset(dx, dy) and t.shape_basic == :Wall and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or tm == :SOIL or tm == :ROOT or (@allow_ice and tm == :FROZEN_LIQUID))
            } }
        end

        # check tile is surrounded by solid walls or visible tile
        def map_tile_nocavern(tile)
            (-1..1).all? { |dx| (-1..1).all? { |dy|
                next if not t = tile.offset(dx, dy)
                tm = t.tilemat
                if !t.designation.hidden
                    t.designation.flow_size < 4 and (@allow_ice or tm != :FROZEN_LIQUID)
                else
                    t.shape_basic == :Wall and (tm == :STONE or tm == :MINERAL or tm == :SOIL or tm == :ROOT or (@allow_ice and tm == :FROZEN_LIQUID))
                end
            } }
        end

        # check tile is a hidden floor
        def map_tile_cavernfloor(t)
            td = t.designation and
            td.hidden and
            td.flow_size == 0 and
            tm = t.tilemat and
            (tm == :STONE or tm == :MINERAL or tm == :SOIL or tm == :ROOT or tm == :GRASS_LIGHT or tm == :GRASS_DARK or tm == :PLANT or tm == :SOIL) and
            t.shape_basic == :Floor
        end

        # create a new Corridor from origin to surface, through rock
        # may create multiple chunks to avoid obstacles, all parts are added to @corridors
        # returns an array of Corridors, 1st = origin, last = surface
        def find_corridor_tosurface(origin)
            cors = []
            while true
                cor = Corridor.new(origin.x, origin.x, origin.y, origin.y, origin.z, origin.z)
                cors.last.accesspath << cor unless cors.empty?
                cors << cor

                cor.z2 += 1 while map_tile_in_rock(cor.maptile2) and not map_tile_intersects_room(cor.maptile2.offset(0, 0, 1))

                if out = cor.maptile2 and (
                    (out.shape_basic != :Ramp and out.shape_basic != :Floor) or
                    out.tilemat == :TREE or
                    out.tilemat == :RAMP or
                    out.designation.flow_size != 0 or
                    out.designation.hidden)

                    out2 = spiral_search(out) { |t|
                        t = t.offset(0, 0, 1) while map_tile_in_rock(t)

                        (t.shape_basic == :Ramp or t.shape_basic == :Floor) and
                        t.tilemat != :TREE and t.designation.flow_size == 0 and
                        not t.designation.hidden and
                        not map_tile_intersects_room(t)
                    }

                    if out.designation.flow_size > 0
                        # damp stone located
                        cor.z2 -= 2
                        out2 = out2.offset(0, 0, -2)
                    else
                        cor.z2 -= 1
                        out2 = out2.offset(0, 0, -1)
                    end

                    if (out2.x - out.x).abs > 1
                        cor = Corridor.new(out2.x - (out2.x <=> out.x), out.x - (out.x <=> out2.x), out2.y, out2.y, out2.z, out2.z)
                        cors.last.accesspath << cor
                        cors << cor
                    end

                    if (out2.y - out.y).abs > 1
                        cor = Corridor.new(out.x - (out.x <=> out2.x), out.x - (out.x <=> out2.x), out2.y + (out.y <=> out2.y), out.y, out2.z, out2.z)
                        cors.last.accesspath << cor
                        cors << cor
                    end

                    raise "find_corridor_tosurface: loop: #{origin.inspect}" if df.same_pos?(origin, out2)
                    ai.debug "find_corridor_tosurface: #{origin.inspect} -> #{out2.inspect}"

                    origin = out2
                else
                    break
                end
            end
            @corridors += cors
            cors
        end

        def surface_tile_at(t, ty=nil, allow_trees=false)
            @rangez ||= (0...df.world.map.z_count).to_a.reverse

            if ty
                tx = t
            else
                tx, ty = t.x, t.y
            end

            dx, dy = tx & 15, ty & 15

            tree = false
            @rangez.each do |z|
                next unless b = df.map_block_at(tx, ty, z)
                next unless tt = b.tiletype[dx][dy]
                next unless ts = DFHack::Tiletype::Shape[tt]
                next unless tsb = DFHack::TiletypeShape::BasicShape[ts]
                next if tsb == :Open
                next unless tm = DFHack::Tiletype::Material[tt]
                return if tm == :POOL or tm == :RIVER
                if tsb == :Floor or tsb == :Ramp
                    return df.map_tile_at(tx, ty, z) if tm != :TREE
                end
                if tm == :TREE
                    tree = true
                elsif tree
                    return df.map_tile_at(tx, ty, z + 1) if allow_trees
                    return
                end
            end
            return
        end

        def status
            status = @tasks.inject(Hash.new(0)) { |h, t| h[t[0]] += 1; h }.map { |t, n| "#{t}: #{n}" }.join(', ')
            if task = digging?
                status << ", digging: #{describe_room(task[1])}"
            end
        end

        def categorize_all
            @room_category = {}
            @rooms.each { |r|
                (@room_category[r.type] ||= []) << r
            }
            @room_category[:stockpile] = @room_category[:stockpile].sort_by { |r|
                [r.misc[:stockpile_level], (r.x1 < @fort_entrance.x ? -r.x1 : r.x1), r.y1]
            } if @room_category[:stockpile]
        end

        def describe_room(r)
            "#{@rooms.index(r) or @corridors.index(r)} #{r}"
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

        def serialize
            {
                :rooms     => @rooms.map(&:serialize),
                :corridors => @corridors.map(&:serialize),
                :tasks     => @tasks.map{ |t| t.map{ |v|
                    if i = $dwarfAI.plan.instance_variable_get(:@rooms).index(v) # XXX
                        [:room, i]
                    elsif i = $dwarfAI.plan.instance_variable_get(:@corridors).index(v) # XXX
                        [:corridor, i]
                    else
                        v
                    end
                } },
            }
        end

        def map_tile_intersects_room(t)
            x, y, z = t.x, t.y, t.z
            @rooms.any? { |r|
                r.safe_include?(x, y, z)
            } or @corridors.any? { |r|
                r.safe_include?(x, y, z)
            }
        end

        def self.find_tree_base(t)
            tree = (df.world.plants.tree_dry.to_a | df.world.plants.tree_wet.to_a).find { |tree|
                next true if tree.pos.x == t.x and tree.pos.y == t.y and tree.pos.z == t.z
                next unless tree.tree_info and tree.tree_info.body
                sx = tree.pos.x - tree.tree_info.dim_x / 2
                sy = tree.pos.y - tree.tree_info.dim_y / 2
                sz = tree.pos.z
                next if t.x < sx or t.y < sy or t.z < sz or t.x >= sx + tree.tree_info.dim_x or t.y >= sy + tree.tree_info.dim_y or t.z >= sz + tree.tree_info.body_height
                next unless tree.tree_info.body[(t.z - sz)]
                tile = tree.tree_info.body[(t.z - sz)][(t.x - sx) + tree.tree_info.dim_x * (t.y - sy)]
                tile._whole != 0 and not tile.blocked
            }
            if tree
                df.map_tile_at(tree)
            else
                $dwarfAI.debug "failed to find tree at #{t.inspect}"
                t
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
                        if dm != :No and t.tilemat == :TREE
                            dm = :Default
                            t = DwarfAI::Plan::find_tree_base(t)
                        end
                        t.dig dm if ((dm == :DownStair or dm == :Channel) and t.shape != :STAIR_DOWN and t.shape_basic != :Open) or
                                t.shape == :WALL
                    end
                } } }
                return if plandig
                @layout.each { |f|
                    if t = df.map_tile_at(x1+f[:x].to_i, y1+f[:y].to_i, z1+f[:z].to_i)
                        next if t.tilemat == :CONSTRUCTION
                        if f[:dig]
                            t.dig f[:dig] if t.shape_basic == :Wall or (f[:dig] == :Channel and t.shape_basic != :Open)
                        else
                            dm = dig_mode(t.x, t.y, t.z)
                            t.dig dm if (dm == :DownStair and t.shape != :STAIR_DOWN) or t.shape == :WALL
                        end
                    end
                }
            end

            def fixup_open
                (x1..x2).each { |x| (y1..y2).each { |y| (z1..z2).each { |z|
                    if f = layout.find { |f|
                        fx, fy, fz = x1 + f[:x].to_i, y1 + f[:y].to_i, z1 + f[:z].to_i
                        fx == x and fy == y and fz == z
                    }
                        fixup_open_tile(x, y, z, f[:dig] || :Default, f) unless f[:construction]
                    else
                        fixup_open_tile(x, y, z, dig_mode(x, y, z))
                    end
                } } }
            end

            def fixup_open_tile(x, y, z, d, f=nil)
                return unless t = df.map_tile_at(x, y, z)
                case d
                when :Channel, :No
                    # do nothing
                when :Default
                     fixup_open_helper(x, y, z, :Floor, f) if t.shape_basic == :Open
                when :UpDownStair, :UpStair, :Ramp
                     fixup_open_helper(x, y, z, d, f) if t.shape_basic == :Open or t.shape_basic == :Floor
                when :DownStair
                     fixup_open_helper(x, y, z, d, f) if t.shape_basic == :Open
                end
            end

            def fixup_open_helper(x, y, z, c, f=nil)
                unless f
                    f = {:x => x - x1, :y => y - y1, :z => z - z1}
                    layout << f
                end
                f[:construction] = c
            end

            def include?(x, y, z)
                x1 <= x and x2 >= x and y1 <= y and y2 >= y and z1 <= z and z2 >= z
            end

            def safe_include?(x, y, z)
                (x1 - 1 <= x and x2 + 1 >= x and y1 - 1 <= y and y2 + 1 >= y and z1 <= z and z2 >= z) or
                layout.any? { |f|
                    fx, fy, fz = x1 + f[:x].to_i, y1 + f[:y].to_i, z1 + f[:z].to_i
                    fx - 1 <= x and fx + 1 >= x and fy - 1 <= y and fy + 1 >= y and fz == z
                }
            end

            def dig_mode(x, y, z)
                return :Default if @type != :corridor
                wantup = include?(x, y, z+1)
                wantdown = include?(x, y, z-1)
                # XXX
                wantup ||= $dwarfAI.plan.instance_variable_get(:@corridors).any? { |r|
                    (accesspath.include?(r) or r.z1 != r.z2) and r.include?(x, y, z+1)
                }
                wantdown ||= $dwarfAI.plan.instance_variable_get(:@corridors).any? { |r|
                    (accesspath.include?(r) or r.z1 != r.z2) and r.include?(x, y, z-1)
                }

                if wantup
                    wantdown ? :UpDownStair : :UpStair
                else
                    wantdown ? :DownStair : :Default
                end
            end

            def dug?(want=nil)
                holes = []
                @layout.each { |f|
                    next if f[:ignore]
                    return if not t = df.map_tile_at(x1+f[:x].to_i, y1+f[:y].to_i, z1+f[:z].to_i)
                    next holes << t if f[:dig] == :No
                    case t.shape_basic
                    when :Wall; return false
                    when :Open
                    else return false if f[:dig] == :Channel
                    end
                }
                (@x1..@x2).each { |x| (@y1..@y2).each { |y| (@z1..@z2).each { |z|
                    if t = df.map_tile_at(x, y, z)
                        return false if (t.shape == :WALL or (want and t.shape_basic != want)) and not holes.find { |h| h.x == t.x and h.y == t.y and h.z == t.z }
                    end
                } } }
                true
            end

            def constructions_done?
                @layout.each { |f|
                    next if not f[:construction]
                    return if not t = df.map_tile_at(x1+f[:x].to_i, y1+f[:y].to_i, z1+f[:z].to_i)
                    # TODO check actual tile shape vs construction type
                    return if t.shape_basic == :Open
                }
            end

            def to_s
                s = type.to_s
                s << " (#{subtype})" if subtype
                if owner and u = df.unit_find(owner)
                    s << " (owned by #{DwarfAI::describe_unit(u)})"
                end
                if misc[:squad_id] and squad = df.world.squads.all.binsearch(misc[:squad_id])
                    s << " (used by #{DwarfAI::capitalize_all(squad.name.to_s(true))})"
                end
                s << " (#{misc[:stockpile_level]})" if misc[:stockpile_level]
                s << " (#{misc[:workshop_level]})" if misc[:workshop_level]
                s << " (#{misc[:workshop]})" if misc[:workshop]
                s << " (#{misc[:users].length} users)" if misc[:users]
                s << " (#{status})"
                s
            end

            def serialize
                {
                    :x1         => x1,
                    :x2         => x2,
                    :y1         => y1,
                    :y2         => y2,
                    :z1         => z1,
                    :z2         => z2,
                    :accesspath => accesspath.map{ |ap|
                        if i = $dwarfAI.plan.instance_variable_get(:@rooms).index(ap) # XXX
                            [:room, i]
                        elsif i = $dwarfAI.plan.instance_variable_get(:@corridors).index(ap) # XXX
                            [:corridor, i]
                        else
                            raise "access path #{ap.inspect} not in @rooms or @corridors"
                        end
                    },
                    :status     => status,
                    :layout     => layout,
                    :type       => type,
                    :owner      => owner,
                    :subtype    => subtype,
                    :misc       => Hash[misc.map{ |k, v|
                        if k == :workshop
                            [k, $dwarfAI.plan.instance_variable_get(:@rooms).index(v)] # XXX
                        else
                            [k, v]
                        end
                    }],
                }
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

# vim: et:sw=4:ts=4
