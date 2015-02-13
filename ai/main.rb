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

    def self.timestamp(y=df.cur_year, t=df.cur_year_tick)
        return '?????-??-??:????' if y == 0 and t == 0
        "#{y.to_s.rjust(5, '0')}-#{(t / 50 / 24 / 28 + 1).to_s.rjust(2, '0')}-#{(t / 50 / 24 % 28 + 1).to_s.rjust(2, '0')}:#{(t % (24 * 50)).to_s.rjust(4, '0')}"
    end

    def self.describe_item(i)
        s = DFHack::StlString.cpp_new
        i.getItemDescription(s, 0)
        str = s.str.to_s
        s._cpp_delete
        str
    end

    def self.describe_unit(u)
        s = capitalize_all(u.name)
        s << ', ' unless s.empty?
        s << profession(u)
        s
    end

    def self.capitalize_all(s)
        s.to_s.split(' ').map { |w|
            if w == 'of' or w == 'the'
                w
            else
                w.sub(/^[a-z]/) { |s| s.upcase }
            end
        }.join(' ')
    end

    # converted from dfhack's C++ api
    def self.profession(u, ignore_noble=false, plural=false)
        prof = u.custom_profession
        return prof unless prof.empty?

        if not ignore_noble and np = df.unit_entitypositions(u) and not np.empty?
            case u.sex
            when 0
                prof = np[0].name_female[plural ? 1 : 0]
            when 1
                prof = np[0].name_male[plural ? 1 : 0]
            end

            prof = np[0].name[plural ? 1 : 0] if prof.empty?
            return prof unless prof.empty?
        end

        current_race = df.ui.race_id
        use_race_prefix = u.race != current_race

        caste = u.caste_tg
        race_prefix = caste.caste_name[0]

        if plural
            prof = caste.caste_profession_name.plural[u.profession]
        else
            prof = caste.caste_profession_name.singular[u.profession]
        end

        if prof.empty?
            case u.profession
            when :CHILD
                prof = caste.child_name[plural ? 1 : 0]
                use_race_prefix = prof.empty?
            when :BABY
                prof = caste.baby_name[plural ? 1 : 0]
                use_race_prefix = prof.empty?
            end
        end

        race = u.race_tg
        race_prefix = race.name[0] if race_prefix.empty?

        if prof.empty?
            if plural
                prof = race.profession_name.plural[u.profession]
            else
                prof = race.profession_name.singular[u.profession]
            end

            if prof.empty?
                case u.profession
                when :CHILD
                    prof = race.general_child_name[plural ? 1 : 0]
                    use_race_prefix = prof.empty?
                when :BABY
                    prof = race.general_baby_name[plural ? 1 : 0]
                    use_race_prefix = prof.empty?
                end
            end
        end

        race_prefix = 'Animal' if race_prefix.empty?

        if prof.empty?
            case u.profession
            when :TRAINED_WAR
                prof = "War #{use_race_prefix ? race_prefix : 'Peasant'}"
                use_race_prefix = false
            when :TRAINED_HUNTER
                prof = "Hunting #{use_race_prefix ? race_prefix : 'Peasant'}"
                use_race_prefix = false
            when :STANDARD
                prof = 'Peasant' unless use_race_prefix
            else
                if caption = DFHack::Profession::Caption[u.profession]
                    prof = caption
                else
                    prof = u.profession.to_s.downcase
                end
            end
        end

        if use_race_prefix
            race_prefix << " " unless prof.empty?
            prof = race_prefix + prof
        end

        capitalize_all(prof)
    end

    def debug(str, announce=nil)
        str = str.join("\n") if Array === str
        df.add_announcement("AI: #{str}", 7, false) { |ann| ann.pos = announce } if announce
        ts = DwarfAI.timestamp
        puts "AI: #{ts} #{str}" if $DEBUG
        unless @logger
            @logger = open('df-ai.log', 'a')
            @logger.sync = true
        end
        @logger.puts "#{ts} #{str.to_s.gsub("\n", "\n                 ")}"
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

                case text
                when /I am your liaison from the Mountainhomes\. Let's discuss your situation\.|Farewell, .*I look forward to our meeting next year\.|A diplomat has left unhappy\.|You have disrespected the trees in this area, but this is what we have come to expect from your stunted kind\. Further abuse cannot be tolerated\. Let this be a warning to you\.|Greetings from the woodlands\. We have much to discuss\.|Although we do not always see eye to eye \(ha!\), I bid you farewell\. May you someday embrace nature as you embrace the rocks and mud\./
                    debug "exit diplomat textviewerst (#{text.inspect})"
                    timeout_sameview {
                        df.curview.feed_keys(:LEAVESCREEN)
                    }

                when /A vile force of darkness has arrived!|The .* have brought the full forces of their lands against you\.|The enemy have come and are laying siege to the fortress\.|The dead walk\. Hide while you still can!/
                    debug "siege (#{text.inspect})"
                    timeout_sameview {
                        df.curview.feed_keys(:LEAVESCREEN)
                        unpause!
                    }

                when /Your strength has been broken\.|Your settlement has crumbled to its end\.|Your settlement has been abandoned\./
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
        last_unpause = Time.now
        @pause_onupdate = df.onupdate_register_once('df-ai unpause') {
            if Time.now < last_unpause + 11
                # do nothing
            elsif df.pause_state
                timeout_sameview(10) { unpause! }
                last_update = Time.now
            end
            false
        }

        df.onstatechange_register_once { |st|
            case st
            when :WORLD_UNLOADED
                debug 'world unloaded, disabling self'
                onupdate_unregister
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
        df.onupdate_unregister(@pause_onupdate)
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
