class DwarfAI
    attr_accessor :onupdate_handle
    attr_accessor :plan
    attr_accessor :citizen

    def update
        update_citizen
        update_plan
    end

    def update_citizen
        @citizen ||= {}
        oldkeys = @citizen.dup

        df.unit_citizens.each { |u|
            @citizen[u.id] ||= Citizen.new(self, u.id)
            oldkeys.delete u.id
        }

        oldkeys.each { |id|
            c = @citizen.delete(id)
	    c.detach
        }
    end

    def update_plan
        @plan ||= Plan.new(self)

        @plan.update
    end

    def onupdate_register
        @onupdate_handle = df.onupdate_register(120) { update }
	df.onstatechange_register { |st| statechanged(st) }
    end

    def statechanged(st)
        if st == :PAUSED
            la = df.world.status.announcements.last
            if la.year == df.cur_year and la.time == df.cur_year_tick
                handle_pause_event(la)
            end
        end
    end

    def pause_event(announce)
        p announce
        #df.pause_state = false
    end

    def onupdate_unregister
        df.onupdate_unregister(@onupdate_handle)
    end

    def info_status
        puts "AI: everything runs according to plans"
    end
end
