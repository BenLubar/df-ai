class DwarfAI
    attr_accessor :onupdate_handle
    attr_accessor :update_counter

    attr_accessor :plan
    attr_accessor :pop

    def initialize
        @pop = Population.new(self)
        @plan = Plan.new(self)

        @plan.setup_blueprint
    end

    def update
        @update_counter += 1
        @pop.update
        @plan.update
    end

    def handle_pause_event(announce)
        case announce.type
        when :MEGABEAST_ARRIVAL
            puts 'AI: uh oh, megabeast...'
            df.pause_state = false
        else
            p announce
            #df.pause_state = false
        end
    end

    def statechanged(st)
        if st == :PAUSED
            la = df.world.status.announcements.last
            if la.year == df.cur_year and la.time == df.cur_year_tick
                handle_pause_event(la)
            end
        end
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
