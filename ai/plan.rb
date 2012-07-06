class DwarfAI
    class Plan
        attr_accessor :ai
        def initialize(ai)
            @ai = ai
        end

        def update
        end

        def new_citizen(c)
            getbedroom(c.id)
        end

        def del_citizen(c)
            freebedroom(c.id)
            # coffin/memorialslab for dead citizen
        end

        def getbedroom(id)
            @rooms.find
        end

        attr_accessor :fort_entrance, :rooms, :corridors, :stairs
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
                            tt = df.map_tile_at(cx+_x+__x, cy+_y+__y, cz) and tt.shape == :FLOOR and tt.designation.flow_size == 0 and not tt.designation.hidden
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

            @fort_entrance = Stairs.new(cx, cy, cz, cz)
        end

        # search how much we need to dig to find a spot for the full fortress body
        # here we cheat and work as if the map was fully reveal()ed
        def scan_fort_body
            # use a hardcoded fort layout
            cx, cy, cz = @fort_entrance.x, @fort_entrance.y, @fort_entrance.z1
            @fort_entrance.z2 = (0..cz).to_a.reverse.find { |cz2|
                (-26..26).all? { |dx|
                    (-22..22).all? { |dy|
                        (-6..1).all? { |dz|
                            t = df.map_tile_at(cx+dx, cy+dy, cz2+dz) and t.shape == :WALL and
                            not t.designation.water_table and (t.tilemat == :STONE or t.tilemat == :MINERAL)
                        }
                    }
                }
            }

            raise 'we need more minerals' if not @fort_entrance.z2
        end

        # assign rooms in the space found by scan_fort_*
        def setup_blueprint_rooms
            @rooms = []
            @corridors = []
            @stairs = []

            # hardcoded layout
            fe = @fort_entrance.dup
            fe.x += 1
            @stairs << @fort_entrance << fe

            # XXX corridors / rooms
        end

        class Room
            attr_accessor :x1, :x2, :y1, :y2, :z, :doors, :type, :owner
            def initialize(x1, x2, y1, y2, z)
                @x1, @x2, @y1, @y2, @z = x1, x2, y1, y2, z
            end
	    alias x x1
	    alias y y1
        end

        class Corridor
            attr_accessor :x1, :x2, :y1, :y2, :z
            def initialize(x1, x2, y1, y2, z)
                @x1, @x2, @y1, @y2, @z = x1, x2, y1, y2, z
            end
	    alias x x1
	    alias y y1
        end

        class Stairs
            attr_accessor :x, :y, :z1, :z2
            def initialize(x, y, z1, z2)
                @x, @y, @z1, @z2 = x, y, z1, z2
            end
	    alias z z1
        end
    end
end
