#include "ai.h"
#include "room.h"

/*
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

def initialize(type, subtype, x1, x2, y1, y2, z)
    super(x1, x2, y1, y2, z, z)
    @type = type
    @subtype = subtype
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

def dfbuilding
    df.building_find(misc[:bld_id]) if misc[:bld_id]
end
*/

// vim: et:sw=4:ts=4
