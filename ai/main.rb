class DwarfAI
    attr_accessor :onupdate_handle_plan, :onupdate_handle_pop

    attr_accessor :plan
    attr_accessor :pop

    def initialize
        @pop = Population.new(self)
        @plan = Plan.new(self)

        @plan.startup
    end

    def handle_pause_event(announce)
        case announce.type
        when :MEGABEAST_ARRIVAL
            puts 'AI: uh oh, megabeast...'
        when :D_MIGRANTS_ARRIVAL
            puts 'AI: more minions'
        when :LIAISON_ARRIVAL, :CARAVAN_ARRIVAL
            puts 'AI: visitors'
        when :STRANGE_MOOD, :MOOD_BUILDING_CLAIMED, :ARTIFACT_BEGUN, :MADE_ARTIFACT
        else
            puts "unhandled pausing announce #{announce.inspect}"
            return
        end
        df.pause_state = false
    end

    def statechanged(st)
        if st == :PAUSED and
                la = df.world.status.announcements.to_a.reverse.find { |a|
                    df.announcements.flags[a.type].PAUSE rescue nil
                } and la.year == df.cur_year and la.time == df.cur_year_tick
            handle_pause_event(la)
        else
            cvname = df.curview._raw_rtti_classname
            @seen_cvname ||= { 'viewscreen_dwarfmodest' => true }
            puts "AI: paused, curview=#{cvname}" if not @seen_cvname[cvname]
            @seen_cvname[cvname] = true
        end
    end

    def onupdate_register
        @update_counter = 0
        @onupdate_handle_plan = df.onupdate_register(120) { @plan.update }
        @onupdate_handle_pop = df.onupdate_register(180, 60) { @pop.update }
        df.onstatechange_register { |st| statechanged(st) }
    end

    def onupdate_unregister
        df.onupdate_unregister(@onupdate_handle_plan)
        df.onupdate_unregister(@onupdate_handle_pop)
    end

    def status
        ["Plan: #{plan.status}", "Pop: #{pop.status}"]
    end
end
