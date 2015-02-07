class DwarfAI
    attr_accessor :plan
    attr_accessor :pop
    attr_accessor :stocks
    attr_accessor :camera
    attr_accessor :random_embark

    def initialize
        @pop = Population.new(self)
        @plan = Plan.new(self)
        @stocks = Stocks.new(self)
        @camera = Camera.new(self)
        @random_embark = RandomEmbark.new(self)
    end

    def timestamp(y=df.cur_year, t=df.cur_year_tick)
        return '?????-??-??:????' if y == 0 and t == 0
        "#{y.to_s.rjust(5, '0')}-#{(t / 50 / 24 / 28 + 1).to_s.rjust(2, '0')}-#{(t / 50 / 24 % 28 + 1).to_s.rjust(2, '0')}:#{(t % (24 * 50)).to_s.rjust(4, '0')}"
    end

    def debug(str)
        ts = timestamp
        puts "AI: #{ts} #{str}" if $DEBUG
        unless @logger
            @logger = open('df-ai.log', 'a')
            @logger.sync = true
        end
        @logger.puts "#{ts} #{str.gsub("\n", "\n                 ")}"
    end

    def startup
        @pop.startup
        @plan.startup
        @stocks.startup
        @camera.startup
        @random_embark.startup
    end

    def unpause!
        until df.world.status.popups.empty?
            df.curview.feed_keys(:CLOSE_MEGA_ANNOUNCEMENT)
        end
        df.curview.feed_keys(:D_PAUSE) if df.pause_state
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
        debug "pause: #{fulltext.inspect}"

        case announce.type
        when :MEGABEAST_ARRIVAL; debug 'pause: uh oh, megabeast...'
        when :BERSERK_CITIZEN; debug 'pause: berserk'
        when :UNDEAD_ATTACK; debug 'pause: i see dead people'
        when :CAVE_COLLAPSE; debug 'pause: kevin?'
        when :DIG_CANCEL_DAMP, :DIG_CANCEL_WARM; debug 'pause: lazy miners'
        when :BIRTH_CITIZEN; debug 'pause: newborn'
        when :BIRTH_ANIMAL
        when :D_MIGRANTS_ARRIVAL, :D_MIGRANT_ARRIVAL, :MIGRANT_ARRIVAL, :NOBLE_ARRIVAL, :FORT_POSITION_SUCCESSION
            debug 'pause: more minions'
        when :DIPLOMAT_ARRIVAL, :LIAISON_ARRIVAL, :CARAVAN_ARRIVAL, :TRADE_DIPLOMAT_ARRIVAL
            debug 'pause: visitors'
        when :STRANGE_MOOD, :MOOD_BUILDING_CLAIMED, :ARTIFACT_BEGUN, :MADE_ARTIFACT
            debug 'pause: mood'
        when :FEATURE_DISCOVERY, :STRUCK_DEEP_METAL; debug 'pause: dig dig dig'
        when :TRAINING_FULL_REVERSION; debug 'pause: born to be wild'
        when :NAMED_ARTIFACT; debug 'pause: hallo'
        else
            if announce.type.to_s =~ /^AMBUSH/
                debug 'pause: an ambush!'
            else
                debug "pause: unhandled pausing event #{announce.type.inspect} #{announce.inspect}"
                #return
            end
        end

        if df.announcements.flags[announce.type].DO_MEGA
            timeout_sameview { unpause! }
        else
            unpause!
        end
    end

    def statechanged(st)
        # automatically unpause the game (only for game-generated pauses)
        if st == :PAUSED and
                la = df.world.status.announcements.to_a.reverse.find { |a|
                    df.announcements.flags[a.type].PAUSE rescue nil
                } and la.year == df.cur_year and la.time == df.cur_year_tick
            handle_pause_event(la)
        elsif st == :PAUSED
            unpause!
            debug "pause without an event"
        elsif st == :VIEWSCREEN_CHANGED
            case cvname = df.curview._rtti_classname
            when :viewscreen_textviewerst
                text = df.curview.formatted_text.map { |t|
                    t.text.to_s.strip.gsub(/\s+/, ' ')
                }.join(' ')

                if text =~ /I am your liaison from the Mountainhomes\. Let's discuss your situation\.|Farewell, .*I look forward to our meeting next year\.|A diplomat has left unhappy\./
                    debug "exit diplomat textviewerst (#{text.inspect})"
                    timeout_sameview {
                        df.curview.feed_keys(:LEAVESCREEN)
                    }

                elsif text =~ /A vile force of darkness has arrived!/
                    debug "siege (#{text.inspect})"
                    timeout_sameview {
                        df.curview.feed_keys(:LEAVESCREEN)
                        unpause!
                    }

                elsif text =~ /Your strength has been broken\.|Your settlement has crumbled to its end\.|Your settlement has been abandoned\./
                    debug 'you just lost the game:'
                    debug text.inspect
                    debug 'Exiting AI.'
                    onupdate_unregister
                    # dont unpause, to allow for 'die'

                else
                    debug "paused in unknown textviewerst #{text.inspect}"
                end

            when :viewscreen_topicmeetingst
                timeout_sameview {
                    df.curview.feed_keys(:OPTION1)
                }
                debug "exit diplomat topicmeetingst"

            when :viewscreen_topicmeeting_takerequestsst, :viewscreen_requestagreementst
                debug "exit diplomat #{cvname}"
                timeout_sameview {
                    df.curview.feed_keys(:LEAVESCREEN)
                }

            else
                @seen_cvname ||= { :viewscreen_dwarfmodest => true }
                debug "paused in unknown viewscreen #{cvname}" if not @seen_cvname[cvname]
                @seen_cvname[cvname] = true
            end
        end
    end

    def abandon!(view=df.curview)
        return unless $AI_RANDOM_EMBARK
        view.child = DFHack::ViewscreenOptionst.cpp_new(:parent => view, :options => [6])
        view = view.child
        view.feed_keys(:SELECT)
        view.feed_keys(:MENU_CONFIRM)
        # current view switches to a textviewer at this point
        df.curview.feed_keys(:SELECT)
    end

    def timeout_sameview(delay=5, &cb)
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
        @random_embark.onupdate_register
        @status_onupdate = df.onupdate_register('df-ai status', 3*28*1200, 3*28*1200) { debug status }

        df.onstatechange_register_once { |st|
            case st
            when :WORLD_UNLOADED
                debug 'world unloaded, disabling self'
                onupdate_unregister
                if @logger
                    @logger.close
                    @logger = nil
                end
                true
            else
                statechanged(st)
                false
            end
        }
    end

    def onupdate_unregister
        @random_embark.onupdate_unregister
        @camera.onupdate_unregister
        @stocks.onupdate_unregister
        @plan.onupdate_unregister
        @pop.onupdate_unregister
        df.onupdate_unregister(@status_onupdate)
    end

    def status
        ["Plan: #{plan.status}", "Pop: #{pop.status}", "Stocks: #{stocks.status}", "Camera: #{camera.status}"]
    end

    def serialize
        {
            :plan   => plan.serialize,
            :pop    => pop.serialize,
            :stocks => stocks.serialize,
            :camera => camera.serialize,
        }
    end
end

# vim: et:sw=4:ts=4
