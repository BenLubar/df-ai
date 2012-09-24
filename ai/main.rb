class DwarfAI
    attr_accessor :onupdate_handles

    attr_accessor :plan
    attr_accessor :pop
    attr_accessor :stocks

    def initialize
        @pop = Population.new(self)
        @plan = Plan.new(self)
        @stocks = Stocks.new(self)
    end

    def handle_pause_event(announce)
        case announce.type
        when :MEGABEAST_ARRIVAL; puts 'AI: uh oh, megabeast...'
        when :BERSERK_CITIZEN; puts 'AI: berserk'
        when :CAVE_COLLAPSE; puts 'AI: kevin?'
        when :DIG_CANCEL_DAMP, :DIG_CANCEL_WARM; puts 'AI: lazy miners'
        when :BIRTH_CITIZEN; puts 'AI: newborn'
        when :BIRTH_ANIMAL
        when :D_MIGRANTS_ARRIVAL, :D_MIGRANT_ARRIVAL, :MIGRANT_ARRIVAL, :NOBLE_ARRIVAL
            puts 'AI: more minions'
        when :DIPLOMAT_ARRIVAL, :LIAISON_ARRIVAL, :CARAVAN_ARRIVAL, :TRADE_DIPLOMAT_ARRIVAL
            puts 'AI: visitors'
        when :STRANGE_MOOD, :MOOD_BUILDING_CLAIMED, :ARTIFACT_BEGUN, :MADE_ARTIFACT
            puts 'AI: mood'
        else
            if announce.type.to_s =~ /^AMBUSH/
                puts 'AI: an ambush!'
            else
                puts "AI: unhandled pausing event #{announce.type.inspect} #{announce.inspect}"
                return
            end
        end

        # unsplit announce text
        fulltext = announce.text
        idx = df.world.status.announcements.index(announce)
        while announce.flags.continuation
            announce = df.world.status.announcements[idx -= 1]
            break if not announce
            fulltext = announce.text + ' ' + fulltext
        end
        puts " #{df.cur_year}:#{df.cur_year_tick} #{fulltext.inspect}"

        df.pause_state = false
    end

    def statechanged(st)
        # automatically unpause the game (only for game-generated pauses)
        if st == :PAUSED and
                la = df.world.status.announcements.to_a.reverse.find { |a|
                    df.announcements.flags[a.type].PAUSE rescue nil
                } and la.year == df.cur_year and la.time == df.cur_year_tick
            handle_pause_event(la)
        else
            case cvname = df.curview._raw_rtti_classname
            when 'viewscreen_textviewerst'
                text = df.curview.text_display.map { |t|
                    t.text.to_s.strip.gsub(/\s+/, ' ')
                }.join(' ')

                if text =~ /I am your liaison from the Mountainhomes\. Let's discuss your situation\.|Farewell, .*I look forward to our meeting next year\.|A diplomat has left unhappy\./
                    puts "AI: exit diplomat textviewerst (#{text.inspect})"
                    df.curview.feed_keys(:LEAVESCREEN)
                else
                    puts "AI: paused in unknown textviewerst #{text.inspect}" if $DEBUG
                end
            when 'viewscreen_topicmeetingst'
                df.curview.feed_keys(:OPTION1)
                #puts "AI: exit diplomat topicmeetingst"
            when 'viewscreen_topicmeeting_takerequestsst', 'viewscreen_requestagreementst'
                puts "AI: exit diplomat #{cvname}"
                df.curview.feed_keys(:LEAVESCREEN)

            else
                @seen_cvname ||= { 'viewscreen_dwarfmodest' => true }
                puts "AI: paused in unknown viewscreen #{cvname}" if not @seen_cvname[cvname] and $DEBUG
                @seen_cvname[cvname] = true
            end
        end
    end

    def onupdate_register
        @onupdate_handles = [
            df.onupdate_register(360, 10) { @pop.update },
            df.onupdate_register(240, 20) { @plan.update },
            df.onupdate_register(2400, 30) { @stocks.update }
        ]

        df.onstatechange_register { |st| statechanged(st) }
    end

    def onupdate_unregister
        @onupdate_handles.each { |h| df.onupdate_unregister(h) }
    end

    def status
        ["Plan: #{plan.status}", "Pop: #{pop.status}", "Stocks: #{stocks.status}"]
    end
end
