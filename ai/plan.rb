class DwarfAI
    class Plan
        attr_accessor :ai
        attr_accessor :tasks
        def initialize(ai)
            @ai = ai
            @tasks = []
        end

        def update
            checkwagons if ai.update_counter % 16 == 4
            checkidle if ai.update_counter % 6 == 2

            nrdig = @tasks.count { |t| t[0] == :digroom }
            @tasks.delete_if { |t|
                case t[0]
                when :wantdig
                    # use precomputed nrdig to account for delete_if and still
                    # ensure first :wantdig is dug first
                    if nrdig < 2
                        digroom(t[1])
                        nrdig += 1
                        true
                    end
                when :digroom
                    if t[1].dug?
                        construct_room(t[1])
                        true
                    end
                when :construct_workshop
                    try_construct_workshop(t[1])
                when :furnish
                    try_furnish(t[1], t[2])
                when :makeroom
                    try_makeroom(t[1])
                when :checkconstruct
                    try_endconstruct(t[1])
                end
            }
        end

        def digging?
            @tasks.find { |t| t[0] == :wantdig or t[0] == :digroom }
        end

        def idle?
            @tasks.empty?
        end

        def new_citizen(uid)
            getbedroom(uid)
            # TODO assign diningroom table
        end

        def del_citizen(uid)
            freebedroom(uid)
            # TODO coffin/memorialslab for dead citizen
        end
        
        def checkwagons
            df.world.buildings.other[:WAGON].each { |w|
                if w.kind_of?(DFHack::BuildingWagonst) and not w.contained_items.find { |i| i.use_mode == 0 }
                    df.building_deconstruct(w)
                end
            }
        end

        def checkidle
            # if nothing better to do, order the miners to dig remaining stockpiles, workshops, and a few bedrooms
            if not digging?
                freebed = 3
                if r = @rooms.find { |_r| _r.type == :stockpile and _r.subtype and _r.status == :plan } ||
                       @rooms.find { |_r| _r.type == :workshop and _r.subtype and _r.status == :plan } ||
                       @rooms.find { |_r| _r.type == :bedroom and not _r.owner and ((freebed -= 1) >= 0) and _r.status == :plan }
                    wantdig(r)
                end
            end
        end

        def getbedroom(id)
            if r = @rooms.find { |_r| _r.type == :bedroom and (not _r.owner or _r.owner == id) } ||
                   @rooms.find { |_r| _r.type == :bedroom and _r.status == :plan and not _r.misc[:queue_dig] }
                wantdig(r)
                set_owner(r, id)
                df.add_announcement("AI: assigned a bedroom to #{df.unit_find(id).name.to_s(false)}", 7, false) { |ann| ann.pos = [r.x+1, r.y+1, r.z] }
            else
                puts "AI cant getbedroom(#{id})"
            end
        end

        def freebedroom(id)
            if r = @rooms.find { |_r| _r.type == :bedroom and _r.owner == id }
                set_owner(r, nil)
            end
        end

        def set_owner(r, uid)
            r.owner = uid
            if r.misc[:bld_id]
                u = df.unit_find(uid) if uid
                df.building_setowner(df.building_find(r.misc[:bld_id]), u)
            end
        end

        def wantdig(r)
            return if r.misc[:queue_dig] or r.status != :plan
            r.misc[:queue_dig] = true
            @tasks << [:wantdig, r]
        end

        def digroom(r)
            r.misc[:queue_dig] = false
            r.dig
            @tasks << [:digroom, r]
            r.layout.each { |f|
                build_furniture(f) if f[:makeroom]
            }
        end

        def construct_room(r)
            case r.type
            when :stockpile
                construct_stockpile(r)
            when :workshop
                @tasks << [:construct_workshop, r]
            else
                r.layout.each { |f|
                    @tasks << [:furnish, r, f] if f[:makeroom]
                }
            end
        end

        FurnitureOtheridx = {}  # :moo => :MOO
        FurnitureRtti = {}      # :moo => :item_moost
        FurnitureBuilding = {}  # :moo => :Moo
        FurnitureWorkshop = {   # :moo => :Masons
            :bed => :Carpenters,
        }
        FurnitureOrder = {      # :moo => :ConstructMoo
            :chair => :ConstructThrone,
        }

        def try_furnish(r, f)
            return true if f[:bld_id]
            build_furniture(f)
            oidx = FurnitureOtheridx[f[:item]] || "#{f[:item]}".upcase.to_sym
            rtti = FurnitureRtti[f[:item]] || "item_#{f[:item]}st".to_sym
            if itm = df.world.items.other[oidx].find { |i| i._rtti_classname == rtti and df.building_isitemfree(i) rescue next }
                bldn = FurnitureBuilding[f[:item]] || "#{f[:item]}".capitalize.to_sym
                bld = df.building_alloc(bldn)
                df.building_position(bld, [r.x+f[:x], r.y+f[:y], r.z])
                df.building_construct(bld, [itm])
                if f[:makeroom]
                    r.misc[:bld_id] = bld.id
                    @tasks << [:makeroom, r]
                end
                f[:bld_id] = bld.id
                true
            end
        end

        def build_furniture(f)
            return if f[:queue_build]
            ws = FurnitureWorkshop[f[:item]] || :Masons
            mod = FurnitureOrder[f[:item]] || "Construct#{f[:item].to_s.capitalize}".to_sym
            ensure_workshop(ws)
            add_manager_order(mod)
            df.cuttrees(:any, 1, true) if ws == :Carpenters    # TODO generic work input
            f[:queue_build] = true
        end

        def ensure_workshop(subtype)
            if not ws = @rooms.find { |r| r.type == :workshop and r.subtype == subtype } or ws.status == :plan
                ws ||= @rooms.find { |r| r.type == :workshop and not r.subtype and r.status == :plan }
                ws.subtype = subtype
                digroom(ws)
                df.add_announcement("AI: new workshop #{subtype}", 7, false) { |ann| ann.pos = [ws.x+1, ws.y+1, ws.z] }

                case subtype
                when :Masons, :Carpenters
                    # add minimal stockpile in front of workshop
                    sptype = {:Masons => :stone, :Carpenters => :wood}[subtype]
                    # XXX hardcoded fort layout
                    y = (ws.layout[0][:y] > 0 ? ws.y2+2 : ws.y1-2)  # check door position
                    sp = Room.new(:stockpile, sptype, ws.x1, ws.x2, y, y, ws.z1)
                    sp.misc[:workshop] = ws
                    @rooms << sp
                    digroom(sp)

                    ensure_stockpile(sptype)
                end
            end
            ws
        end

        def ensure_stockpile(sptype)
            if sp = @rooms.find { |r| r.type == :stockpile and r.subtype == sptype and not r.misc[:workshop] }
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
            when :Dyers, :Ashery, :MetalsmithsForge, :WoodFurnace, :Smelter
                # need special items
                if bld = df.building_find(r) and (bld.getSubtype == (bld.getType == :Workshop ? DFHack::WorkshopType : DFHack::FurnaceType).int(r.subtype))
                    r.misc[:bld_id] = bld.id
                    @tasks << [:checkconstruct, r]
                    df.add_announcement("AI: thank you for #{r.subtype}", 7, false) { |ann| ann.pos = [r.x+1, r.y+1, r.z] }
                    true
                elsif !r.misc[:ask_player]
                    df.add_announcement("AI: please manually build a #{r.subtype} here") { |ann| ann.pos = [r.x+1, r.y+1, r.z] }
                    r.misc[:ask_player] = true
                    false
                end
            else
                if bould = df.map_tile_at(r).mapblock.items.map { |idx| df.item_find(idx) }.find { |i|
                        i.kind_of?(DFHack::ItemBoulderst) and df.building_isitemfree(i) and
                        !df.ui.economic_stone[i.mat_index] and
                        i.pos.x >= r.x1 and i.pos.x <= r.x2 and i.pos.y >= r.y1 and i.pos.y <= r.y2
                } || df.world.items.other[:BOULDER].find { |i|
                        i.kind_of?(DFHack::ItemBoulderst) and df.building_isitemfree(i) and
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
            df.building_position(bld, [r.x1, r.y1, r.z], r.w, r.h)
            bld.room.extents = df.malloc(r.w*r.h)
            bld.room.x = r.x1
            bld.room.y = r.y1
            bld.room.width = r.w
            bld.room.height = r.h
            r.w.times { |x| r.h.times { |y| bld.room.extents[x+r.w*y] = 1 } }
            df.building_construct_abstract(bld)
            r.misc[:bld_id] = bld.id
            r.status = :finished
            r.layout.each { |d| @tasks << [:furnish, r, d] }

            setup_stockpile_settings(r, bld)

            if r.misc[:workshop]
                if main = @rooms.find { |o| o.type == :stockpile and o.subtype == r.subtype and not o.misc[:workshop] } and mb = main.dfbuilding
                    mb.give_to << bld
                    bld.take_from << mb
                end
            else
                @rooms.each { |o|
                    if o.type == :stockpile and o.subtype == r.subtype and o.misc[:workshop] and sub = o.dfbuilding
                        bld.give_to << sub
                        sub.take_from << bld
                    end
                }
            end
        end

        def setup_stockpile_settings(r, bld)
            case r.subtype
            when :stone
                bld.settings.flags.stone = true
                case r.misc[:workshop]
                when :Masons
                    df.world.raws.inorganics.length.times { |i| bld.settings.stone[i] = (df.ui.economic_stone[i] ? 0 : 1) }
                else
                    df.world.raws.inorganics.length.times { |i| bld.settings.stone[i] = 1 }
                end
                bld.max_wheelbarrows = 1 if r.w > 1 and r.h > 1
            when :wood
                bld.settings.flags.wood = true
                df.world.raws.plants.all.length.times { |i| bld.settings.wood[i] = 1 }
            when :furniture, :goods, :ammo, :weapons, :armor
                case r.subtype
                when :furniture
                    bld.settings.flags.furniture = true
                    s = bld.settings.furniture
                    s.sand_bags = true
                    60.times { |i| s.type[i] = true }   # len = 33
                when :goods
                    bld.settings.flags.goods = true
                    s = bld.settings.finished_goods
                    60.times { |i| s.type[i] = true }
                when :ammo
                    bld.settings.flags.ammo = true
                    s = bld.settings.ammo
                    60.times { |i| s.type[i] = true }
                when :weapons
                    bld.settings.flags.weapons = true
                    s = bld.settings.weapons
                    60.times { |i| s.weapon_type[i] = true }
                    60.times { |i| s.trapcomp_type[i] = true }
                    s.usable = true
                    s.unusable = true
                when :armor
                    bld.settings.flags.armor = true
                    s = bld.settings.armor
                    60.times { |i| s.body[i] = true }
                    60.times { |i| s.head[i] = true }
                    60.times { |i| s.feet[i] = true }
                    60.times { |i| s.hands[i] = true }
                    60.times { |i| s.legs[i] = true }
                    60.times { |i| s.shield[i] = true }
                    s.usable = true
                    s.unusable = true
                end
                30.times { |i| s.other_mats[i] = true }    # len = 15
                df.world.raws.inorganics.length.times { |i| s.mats[i] = true }
                s.quality_core.map! { true }
                s.quality_total.map! { true }
            when :animals
                bld.settings.flags.animals = true
                bld.settings.animals.animals_empty_cages = true
                bld.settings.animals.animals_empty_traps = true
                df.world.raws.creatures.all.length.times { |i| bld.settings.animals.enabled[i] = true }
            when :refuse, :corpses
                if r.subtype == :corpses
                    c = true
                    bld.settings.flags.corpses = true
                else
                    bld.settings.flags.refuse = true
                    bld.settings.refuse.fresh_raw_hide = false
                    bld.settings.refuse.rotten_raw_hide = true
                end
                t = bld.settings.refuse.type
                200.times { |i| t.type[i] = true }
                df.world.raws.creatures.all.length.times { |i|
                    t.corpses[i] = c
                    t.body_parts[i] = !c
                    t.skulls[i] = !c
                    t.bones[i] = !c
                    t.hair[i] = !c
                    t.shells[i] = !c
                    t.teeth[i] = !c
                    t.horns[i] = !c
                }
            when :food
                bld.settings.flags.food = true
                bld.settings.food.prepared_meals = true
                t = bld.settings.food.type
                # meat[11000], *fish, egg, drink_animal, cheese*, powder_creature, glob[1800], glob*, liquid*
                df.world.raws.plants.all.length.times { |i|
                    t.plants[i] = true
                    t.drink_plant[i] = true
                    t.seeds[i] = true
                    t.leaves[i] = true
                    t.powder_plant[i] = true
                }
            when :cloth
                bld.settings.flags.cloth = true
                100.times { |i|
                    bld.settings.cloth.thread_silk[i] = true    # 53
                    bld.settings.cloth.cloth_silk[i] = true
                    next if i > 10
                    bld.settings.cloth.thread_plant[i] = true   # 2
                    bld.settings.cloth.cloth_plant[i] = true
                    bld.settings.cloth.thread_yarn[i] = true    # 4
                    bld.settings.cloth.cloth_yarn[i] = true
                    bld.settings.cloth.thread_metal[i] = true   # 1
                    bld.settings.cloth.cloth_metal[i] = true
                }
            when :leather
                bld.settings.flags.leather = true
                df.world.raws.creatures.all.length.times { |i| bld.settings.leather[i] = true }
            when :gems, :bars, :coins
                case r.subtype
                when :gems
                    bld.settings.flags.gems = true
                when :bars
                    bld.settings.flags.bars = true
                when :coins
                    bld.settings.flags.coins = true
                end
                df.add_announcement("AI: please manually configure a #{r.subtype} stockpile here") { |ann| ann.pos = [r.x+r.w/2, r.y+r.h/2, r.z] }
            end
        end

        def try_makeroom(r)
            bld = r.dfbuilding
            if not bld
                puts "AI: destroyed building ? #{r.inspect}"
                return true
            end
            return if bld.getBuildStage < bld.getMaxBuildStage

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
                x = r.x1 + d[:x]
                y = r.y1 + d[:y]
                set_ext[x, y, 0]
                # tile in front of the door tile is 4   (TODO door in corner...)
                set_ext[x+1, y, 4] if x < r.x1
                set_ext[x-1, y, 4] if x > r.x2
                set_ext[x, y+1, 4] if y < r.y1
                set_ext[x, y-1, 4] if y > r.y2
            }
            bld.is_room = 1

            set_owner(r, r.owner)

            r.layout.each { |f| @tasks << [:furnish, r, f] }

            r.status = :finished
            true
        end

        def try_endconstruct(r)
            bld = r.dfbuilding
            return if bld and bld.getBuildStage < bld.getMaxBuildStage
            r.layout.each { |d| @tasks << [:furnish, r, d] }
            r.status = :finished
            true
        end

        def add_manager_order(order, amount=1)
            if not o = df.world.manager_orders.find { |_o| _o.job_type == order and _o.amount_total < 4 }
                o = DFHack::ManagerOrder.cpp_new(:job_type => order, :unk_2 => -1, :item_subtype => -1,
                        :mat_type => -1, :mat_index => -1, :hist_figure_id => -1, :amount_left => amount, :amount_total => amount)
                case order
                when :ConstructBed  # wood
                    o.material_category.wood = true
                when :ConstructTable, :ConstructThrone, :ConstructCabinet, :ConstructDoor  # rock
                    o.mat_type = 0
                else
                    p [:unknown_manager_material, order]
                end
                df.world.manager_orders << o
            else
                o.amount_total += amount
                o.amount_left += amount
            end
        end

        attr_accessor :fort_entrance, :rooms, :corridors
        def setup_blueprint
            # TODO use existing fort facilities (so we can relay the user or continue from a save)
            puts 'AI: setting up fort blueprint'
            scan_fort_entrance
            puts 'AI: blueprint found entrance'
            scan_fort_body
            puts 'AI: blueprint found body'
            setup_blueprint_rooms
            puts 'AI: blueprint found rooms'
        end

        # search a valid tile for fortress entrance
        def scan_fort_entrance
            # map center
            cx = df.world.map.x_count / 2
            cy = df.world.map.y_count / 2
            rangex = (-cx..cx).sort_by { |_x| _x.abs }
            rangey = (-cy..cy).sort_by { |_y| _y.abs }
            rangez = (0...df.world.map.z_count).to_a.reverse

            bestdist = 100000
            off = rangex.map { |_x|
                # test the whole map for 4x3 clean spots
                dy = rangey.find { |_y|
                    # can break because rangey is sorted by dist
                    break if _x.abs + _y.abs > bestdist
                    cz = rangez.find { |z|
                        t = df.map_tile_at(cx+_x, cy+_y, z) and t.shape == :FLOOR
                    }
                    next if not cz
                    (-1..2).all? { |__x|
                        (-1..1).all? { |__y|
                            t = df.map_tile_at(cx+_x+__x, cy+_y+__y, cz-1) and t.shape == :WALL and
                            tt = df.map_tile_at(cx+_x+__x, cy+_y+__y, cz) and tt.shape == :FLOOR and tt.designation.flow_size == 0 and not tt.designation.hidden and not df.building_find(tt)
                        }
                    }
                }
                bestdist = [_x.abs + dy.abs, bestdist].min if dy
                [_x, dy] if dy
                # find the closest to the center of the map
            }.compact.sort_by { |dx, dy| dx.abs + dy.abs }.first

            if off
                cx += off[0]
                cy += off[1]
            else
                puts 'AI: cant find fortress entrance spot'
            end
            cz = rangez.find { |z| t = df.map_tile_at(cx, cy, z) and t.shape == :FLOOR }

            @fort_entrance = Corridor.new(cx, cx+1, cy, cy, cz, cz)
        end

        # search how much we need to dig to find a spot for the full fortress body
        # here we cheat and work as if the map was fully reveal()ed
        def scan_fort_body
            # use a hardcoded fort layout
            cx, cy, cz = @fort_entrance.x, @fort_entrance.y, @fort_entrance.z
            @fort_entrance.z1 = (0..cz).to_a.reverse.find { |cz1|
                (-35..35).all? { |dx|
                    (-22..22).all? { |dy|
                        (-5..1).all? { |dz|
                            t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                            not t.designation.water_table and (t.tilemat == :STONE or t.tilemat == :MINERAL or (dz > -2 and t.tilemat == :SOIL))
                        }
                    }
                }
            }

            raise 'we need more minerals' if not @fort_entrance.z1
        end

        # assign rooms in the space found by scan_fort_*
        def setup_blueprint_rooms
            @rooms = []
            @corridors = []

            # hardcoded layout
            @corridors << @fort_entrance

            fx = @fort_entrance.x1
            fy = @fort_entrance.y1

            fz = @fort_entrance.z1
            setup_blueprint_workshops(fx, fy, fz, [@fort_entrance])
            
            fz = @fort_entrance.z1 -= 1
            setup_blueprint_stockpiles(fx, fy, fz, [@fort_entrance])
            
            fz = @fort_entrance.z1 -= 1
            setup_blueprint_utilities(fx, fy, fz, [@fort_entrance])
            
            2.times {
                fz = @fort_entrance.z1 -= 1
                setup_blueprint_bedrooms(fx, fy, fz, [@fort_entrance])
            }
        end

        def setup_blueprint_workshops(fx, fy, fz, entr)
            corridor_center = Corridor.new(fx-2, fx+2, fy-1, fy+1, fz, fz) 
            corridor_center.accesspath = entr
            @corridors << corridor_center

            # Quern, Millstone, Siege, Custom/soapmaker, Custom/screwpress
            # GlassFurnace, Kiln, magma workshops/furnaces, other nobles offices
            types = [:Still,:Kitchen, :Fishery,:Butchers, :Leatherworks,:Tanners,
                :Loom,:Clothiers, :Dyers,:Bowyers, nil,nil]
            types += [:Masons,:Carpenters, :Mechanics,:Farmers, :Craftsdwarfs,:Jewelers,
                :Ashery,:MetalsmithsForge, :WoodFurnace,:Smelter, :ManagersOffice,:BookkeepersOffice]

            [-1, 1].each { |dirx|
                prev_corx = corridor_center
                ocx = fx + dirx*3
                (1..6).each { |dx|
                    # segments of the big central horizontal corridor
                    cx = fx + dirx*(4*dx-1)
                    if dx <= 5
                        cor_x = Corridor.new(ocx, cx, fy-1, fy+1, fz, fz)
                        cor_x.accesspath = [prev_corx]
                        @corridors << cor_x
                    else
                        # last 2 workshops of the row (offices etc) get only a narrow/direct corridor
                        cor_x = Corridor.new(fx+dirx*3, cx, fy, fy, fz, fz)
                        cor_x.accesspath = [corridor_center]
                        @corridors << cor_x
                        cor_x = Corridor.new(cx, cx, fy-1, fy+1, fz, fz)
                        cor_x.accesspath = [@corridors.last]
                        @corridors << cor_x
                    end
                    prev_corx = cor_x
                    ocx = cx+dirx

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
            corridor_center = Corridor.new(fx-2, fx+2, fy-1, fy+1, fz, fz) 
            corridor_center.accesspath = entr
            @corridors << corridor_center

            types = [:wood,:stone, :furniture,:goods, :gems,:weapons, :refuse,:corpses]
            types += [:food,:ammo, :cloth,:leather, :bars,:armor, :animals,:coins]

            # TODO side stairs to workshop level ?
            [-1, 1].each { |dirx|
                prev_corx = corridor_center
                ocx = fx + dirx*3
                (1..4).each { |dx|
                    # segments of the big central horizontal corridor
                    cx = fx + dirx*(8*dx-4)
                    cor_x = Corridor.new(ocx, cx+dirx, fy-1, fy+1, fz, fz)
                    cor_x.accesspath = [prev_corx]
                    @corridors << cor_x
                    prev_corx = cor_x
                    ocx = cx+2*dirx

                    @rooms << Room.new(:stockpile, types.shift, cx-3, cx+3, fy-11, fy-3, fz)
                    @rooms << Room.new(:stockpile, types.shift, cx-3, cx+3, fy+3, fy+11, fz)
                    @rooms[-2, 2].each { |r|
                        r.accesspath = [cor_x]
                        r.layout << {:item => :door, :x => 2, :y => 4-5*(r.y<=>fy)}
                        r.layout << {:item => :door, :x => 4, :y => 4-5*(r.y<=>fy)}
                    }
                }
            }
        end

        def setup_blueprint_utilities(fx, fy, fz, entr)
            corridor_center = Corridor.new(fx-2, fx+2, fy-1, fy+1, fz, fz) 
            corridor_center.accesspath = entr
            @corridors << corridor_center

            # TODO
            # dining room
            # infirmary
            # cemetary
            # well
            # military
            # farmplots?
        end

        def setup_blueprint_bedrooms(fx, fy, fz, entr)
            corridor_center = Corridor.new(fx-2, fx+2, fy-1, fy+1, fz, fz) 
            corridor_center.accesspath = entr
            @corridors << corridor_center

            [-1, 1].each { |dirx|
                prev_corx = corridor_center
                ocx = fx + dirx*3
                (1..3).each { |dx|
                    # segments of the big central horizontal corridor
                    cx = fx + dirx*(11*dx-4)
                    cor_x = Corridor.new(ocx, cx, fy-1, fy+1, fz, fz)
                    cor_x.accesspath = [prev_corx]
                    @corridors << cor_x
                    prev_corx = cor_x
                    ocx = cx+dirx

                    [-1, 1].each { |diry|
                        prev_cory = cor_x
                        ocy = fy + diry*2
                        (1..5).each { |dy|
                            cy = fy + diry*4*dy
                            cor_y = Corridor.new(cx, cx-dirx*1, ocy, cy, fz, fz)
                            cor_y.accesspath = [prev_cory]
                            @corridors << cor_y
                            prev_cory = cor_y
                            ocy = cy+diry

                            @rooms << Room.new(:bedroom, nil, cx-dirx*5, cx-dirx*3, cy-1, cy+1, fz)
                            @rooms << Room.new(:bedroom, nil, cx+dirx*2, cx+dirx*4, cy-1, cy+1, fz)
                            @rooms[-2, 2].each { |r|
                                r.accesspath = [cor_y]
                                r.layout << {:item => :bed, :x => 1, :y => 1, :makeroom => true}
                                r.layout << {:item => :cabinet, :x => 1+(r.x<=>cx), :y => 1+diry}
                                r.layout << {:item => :door, :x => 1-2*(r.x<=>cx), :y => 1}
                            }
                        }
                    }
                }
            }
        end

        class Corridor
            attr_accessor :x1, :x2, :y1, :y2, :z1, :z2, :accesspath, :status
            attr_accessor :misc
            def x; x1; end
            def y; y1; end
            def z; z1; end
            def w; x2-x1+1; end
            def h; y2-y1+1; end
            def h_z; z2-z1; end

            def initialize(x1, x2, y1, y2, z1, z2)
                @misc = {}
                @status = :plan
                @accesspath = []
                x1, x2 = x2, x1 if x1 > x2
                y1, y2 = y2, y1 if y1 > y2
                z1, z2 = z2, z1 if z1 > z2
                @x1, @x2, @y1, @y2, @z1, @z2 = x1, x2, y1, y2, z1, z2
            end

            def dig
                return if @status != :plan
                @status = :dig
                accesspath.to_a.each { |ap| ap.dig if ap.status == :plan }
                (@x1..@x2).each { |x| (@y1..@y2).each { |y| (@z1..@z2).each { |z|
                    if t = df.map_tile_at(x, y, z)
                        dm = dig_mode(t.x, t.y, t.z)
                        t.dig dm if dm == :DownStair or t.shape == :WALL
                    end
                } } }
            end

            def dig_mode(x, y, z)
                wantup = wantdown = false
                wantup = true if z < z2
                wantdown = true if z > z1
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

            def dug?
                (@x1..@x2).each { |x| (@y1..@y2).each { |y| (@z1..@z2).each { |z|
                    if t = df.map_tile_at(x, y, z)
                        return false if t.shape == :WALL
                    end
                } } }
                @status = :dug
                true
            end
        end

        class Room < Corridor
            attr_accessor :layout, :owner, :type, :subtype
            def initialize(type, subtype, x1, x2, y1, y2, z)
                super(x1, x2, y1, y2, z, z)
                @type = type
                @subtype = subtype
                @layout = []
            end

            def dig
                super
                @layout.each { |d|
                    if t = df.map_tile_at(x1+d[:x], y1+d[:y], z)
                        dm = dig_mode(t.x, t.y, t.z)
                        t.dig dm if dm == :DownStair or t.shape == :WALL
                    end
                }
            end

            def dug?
                @layout.each { |d|
                    if t = df.map_tile_at(x1+d[:x], y1+d[:y], z)
                        return false if t.shape == :WALL
                    end
                }
                super
            end

            def dfbuilding
                df.building_find(misc[:bld_id]) if misc[:bld_id]
            end
        end
    end
end
