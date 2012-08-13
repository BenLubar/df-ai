class DwarfAI
    class Plan
        attr_accessor :ai
        attr_accessor :tasks
        def initialize(ai)
            @ai = ai
            @tasks = []

            if w = df.world.buildings.all.grep(DFHack::BuildingWagonst).first
                df.building_deconstruct(w)
            end
        end

        def update
            @tasks.each { |t| }
        end

        def new_citizen(c)
            getbedroom(c.id)
        end

        def del_citizen(c)
            freebedroom(c.id)
            # coffin/memorialslab for dead citizen
        end

        def getbedroom(id)
            if r = @rooms.find { |_r| _r.type == :bedroom and (not _r.owner or _r.owner == id) } || @rooms.find { |_r| _r.type == :bedroom and _r.status == :plan }
                if r.status == :plan
                    @tasks << [:dig, r]
                    # TODO prepare furniture
                end
                r.dig
                r.owner = id
                df.add_announcement "AI: assigned a bedroom to #{df.unit_find(id).name}"
                df.world.status.reports.last.pos = [r.x+1, r.y+1, r.z]
            else
                puts "AI cant getbedroom(#{id})"
            end
        end

        def freebedroom(id)
            if r = @rooms.find { |_r| r.type == :bedroom and r.owner == id }
                r.owner = nil
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

            [-1, 1].each { |dirx|
                prev_corx = corridor_center
                ocx = fx + dirx*3
                (1..5).each { |dx|
                    # segments of the big central horizontal corridor
                    cx = fx + dirx*(4*dx-1)
                    cor_x = Corridor.new(ocx, cx, fy-1, fy+1, fz, fz)
                    cor_x.accesspath = [prev_corx]
                    @corridors << cor_x
                    prev_corx = cor_x
                    ocx = cx+dirx

                    @rooms << Room.new(:workshop, cx-1, cx+1, fy-5, fy-3, fz, [[cx, fy-2, fz]])
                    @rooms << Room.new(:workshop, cx-1, cx+1, fy+3, fy+5, fz, [[cx, fy+2, fz]])
                    @rooms[-2, 2].each { |r| r.accesspath = [cor_x] }
                }
            }
        end

        def setup_blueprint_stockpiles(fx, fy, fz, entr)
            corridor_center = Corridor.new(fx-2, fx+2, fy-1, fy+1, fz, fz) 
            corridor_center.accesspath = entr
            @corridors << corridor_center

            # TODO side stairs to workshop level ?
            [-1, 1].each { |dirx|
                prev_corx = corridor_center
                ocx = fx + dirx*3
                (1..3).each { |dx|
                    # segments of the big central horizontal corridor
                    cx = fx + dirx*(8*dx-4)
                    cor_x = Corridor.new(ocx, cx+dirx, fy-1, fy+1, fz, fz)
                    cor_x.accesspath = [prev_corx]
                    @corridors << cor_x
                    prev_corx = cor_x
                    ocx = cx+2*dirx

                    @rooms << Room.new(:stockpile, cx-3, cx+3, fy-11, fy-3, fz, [[cx-1, fy-2, fz], [cx+1, fy-2, fz]])
                    @rooms << Room.new(:stockpile, cx-3, cx+3, fy+3, fy+11, fz, [[cx-1, fy+2, fz], [cx+1, fy+2, fz]])
                    @rooms[-2, 2].each { |r| r.accesspath = [cor_x] }
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

                            @rooms << Room.new(:bedroom, cx-dirx*5, cx-dirx*3, cy-1, cy+1, fz, [[cx-dirx*2, cy, fz]])
                            @rooms << Room.new(:bedroom, cx+dirx*2, cx+dirx*4, cy-1, cy+1, fz, [[cx+dirx*1, cy, fz]])
                            @rooms[-2, 2].each { |r| r.accesspath = [cor_y] }
                        }
                    }
                }
            }
        end

        class Corridor
            attr_accessor :x1, :x2, :y1, :y2, :z1, :z2, :accesspath, :status
            def x; x1; end
            def y; y1; end
            def z; z1; end
            def w; x2-x1; end
            def h; y2-y1; end
            def h_z; z2-z1; end

            def initialize(x1, x2, y1, y2, z1, z2)
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
                        t.dig dig_mode(x, y, z)
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
        end

        class Room < Corridor
            attr_accessor :doors, :type, :owner, :furniture
            def initialize(type, x1, x2, y1, y2, z, doors=[])
                super(x1, x2, y1, y2, z, z)
                @type = type
                @doors = doors
                @furniture = []
            end

            def dig
                super
                @doors.each { |x, y, z|
                    if t = df.map_tile_at(x, y, z)
                        t.dig dig_mode(x, y, z)
                    end
                }
            end
        end
    end
end
