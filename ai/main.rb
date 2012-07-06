class DwarfAI
    attr_accessor :onupdate_handle
    attr_accessor :update_counter

    attr_accessor :plan
    attr_accessor :citizen

    def initialize
        @plan = Plan.new(self)
        @citizen = {}
    end


    def update
        @update_counter += 1
        update_citizenlist
        update_plan
    end

    def update_citizenlist
        old = @citizen.dup

        # add new fort citizen to our list
        df.unit_citizens.each { |u|
            @citizen[u.id] ||= Citizen.new(self, u.id)
            old.delete u.id
        }

        # del those who are no longer here
        old.each { |id, c|
	    c.detach
            @citizen.delete(id)
        }
    end

    def update_plan
        @plan.update
    end

    def info_status
        puts "AI: everything runs according to plans"
    end


    def statechanged(st)
        if st == :PAUSED
            la = df.world.status.announcements.last
            if la.year == df.cur_year and la.time == df.cur_year_tick
                handle_pause_event(la)
            end
        end
    end

    def handle_pause_event(announce)
        p announce
        #df.pause_state = false
    end


    def onupdate_register
        @update_counter = 0
        @onupdate_handle = df.onupdate_register(120) { update }
	df.onstatechange_register { |st| statechanged(st) }
    end

    def onupdate_unregister
        df.onupdate_unregister(@onupdate_handle)
    end
end
