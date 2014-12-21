class DwarfAI
    attr_accessor :plan
    attr_accessor :pop
    attr_accessor :stocks
    attr_accessor :camera

    def initialize
        @pop = Population.new(self)
        @plan = Plan.new(self)
        @stocks = Stocks.new(self)
        @camera = Camera.new(self)
    end

    def debug(str)
        puts "AI: #{df.cur_year}:#{df.cur_year_tick} #{str}" if $DEBUG
    end

    def startup
        @pop.startup
        @plan.startup
        @stocks.startup
        @camera.startup
    end

    def handle_pause_event(announce)
        # unsplit announce text
        fulltext = announce.text
        idx = df.world.status.announcements.index(announce)
        while announce.flags.continuation
            announce = df.world.status.announcements[idx -= 1]
            break if not announce
            fulltext = announce.text + ' ' + fulltext
        end
        puts " #{df.cur_year}:#{df.cur_year_tick} #{fulltext.inspect}"

        case announce.type
        when :MEGABEAST_ARRIVAL; puts 'AI: uh oh, megabeast...'
        when :BERSERK_CITIZEN; puts 'AI: berserk'
        when :UNDEAD_ATTACK; puts 'AI: i see dead people'
        when :CAVE_COLLAPSE; puts 'AI: kevin?'
        when :DIG_CANCEL_DAMP, :DIG_CANCEL_WARM; puts 'AI: lazy miners'
        when :BIRTH_CITIZEN; puts 'AI: newborn'
        when :BIRTH_ANIMAL
        when :D_MIGRANTS_ARRIVAL, :D_MIGRANT_ARRIVAL, :MIGRANT_ARRIVAL, :NOBLE_ARRIVAL, :FORT_POSITION_SUCCESSION
            puts 'AI: more minions'
        when :DIPLOMAT_ARRIVAL, :LIAISON_ARRIVAL, :CARAVAN_ARRIVAL, :TRADE_DIPLOMAT_ARRIVAL
            puts 'AI: visitors'
        when :STRANGE_MOOD, :MOOD_BUILDING_CLAIMED, :ARTIFACT_BEGUN, :MADE_ARTIFACT
            puts 'AI: mood'
        when :FEATURE_DISCOVERY, :STRUCK_DEEP_METAL; puts 'AI: dig dig dig'
        when :TRAINING_FULL_REVERSION; puts 'AI: born to be wild'
        when :NAMED_ARTIFACT; puts 'AI: hallo'
        else
            if announce.type.to_s =~ /^AMBUSH/
                puts 'AI: an ambush!'
            else
                puts "AI: unhandled pausing event #{announce.type.inspect} #{announce.inspect}"
                #return
            end
        end

        df.curview.feed_keys(:CLOSE_MEGA_ANNOUNCEMENT)
        df.pause_state = false
    end

    def statechanged(st)
        # automatically unpause the game (only for game-generated pauses)
        if st == :PAUSED and
                la = df.world.status.announcements.to_a.reverse.find { |a|
                    df.announcements.flags[a.type].PAUSE rescue nil
                } and la.year == df.cur_year and la.time == df.cur_year_tick
            handle_pause_event(la)
        elsif st == :PAUSED
            df.curview.feed_keys(:CLOSE_MEGA_ANNOUNCEMENT)
            df.pause_state = false
        elsif st == :VIEWSCREEN_CHANGED
            case cvname = df.curview._rtti_classname
            when :viewscreen_textviewerst
                text = df.curview.formatted_text.map { |t|
                    t.text.to_s.strip.gsub(/\s+/, ' ')
                }.join(' ')

                if text =~ /I am your liaison from the Mountainhomes\. Let's discuss your situation\.|Farewell, .*I look forward to our meeting next year\.|A diplomat has left unhappy\./
                    puts "AI: exit diplomat textviewerst (#{text.inspect})"
                    timeout_sameview {
                        df.curview.feed_keys(:LEAVESCREEN)
                    }

                elsif text =~ /A vile force of darkness has arrived!/
                    puts "AI: siege (#{text.inspect})"
                    timeout_sameview {
                        df.curview.feed_keys(:LEAVESCREEN)
                        df.pause_state = false
                    }

                elsif text =~ /Your strength has been broken\.|Your settlement has crumbled to its end\./
                    puts "AI: you just lost the game:", text.inspect, "Exiting AI."
                    onupdate_unregister
                    # dont unpause, to allow for 'die'

                else
                    puts "AI: paused in unknown textviewerst #{text.inspect}" if $DEBUG
                end

            when :viewscreen_topicmeetingst
                timeout_sameview {
                    df.curview.feed_keys(:OPTION1)
                }
                #puts "AI: exit diplomat topicmeetingst"

            when :viewscreen_topicmeeting_takerequestsst, :viewscreen_requestagreementst
                puts "AI: exit diplomat #{cvname}"
                timeout_sameview {
                    df.curview.feed_keys(:LEAVESCREEN)
                }

            else
                @seen_cvname ||= { :viewscreen_dwarfmodest => true }
                puts "AI: paused in unknown viewscreen #{cvname}" if not @seen_cvname[cvname] and $DEBUG
                @seen_cvname[cvname] = true
            end
        end
    end

    def timeout_sameview(delay=8, &cb)
        curscreen = df.curview._rtti_classname
        timeoff = Time.now + delay

        df.onupdate_register_once('timeout') {
            next true if df.curview._rtti_classname != curscreen

            if Time.now >= timeoff
                cb.call
                true
            end
        }
    end

    def onupdate_register
        @pop.onupdate_register
        @plan.onupdate_register
        @stocks.onupdate_register
        @camera.onupdate_register
        @status_onupdate = df.onupdate_register('df-ai status', 3*28*1200, 3*28*1200) { puts status }

        df.onstatechange_register_once { |st|
            case st
            when :WORLD_UNLOADED
                puts 'AI: world unloaded, disabling self'
                onupdate_unregister
                true
            else
                statechanged(st)
                false
            end
        }
    end

    def onupdate_unregister
        @camera.onupdate_unregister
        @stocks.onupdate_unregister
        @plan.onupdate_unregister
        @pop.onupdate_unregister
        df.onupdate_unregister(@status_onupdate)
    end

    def status
        ["Plan: #{plan.status}", "Pop: #{pop.status}", "Stocks: #{stocks.status}", "Camera: #{camera.status}"]
    end
end

# vim: et:sw=4:ts=4
