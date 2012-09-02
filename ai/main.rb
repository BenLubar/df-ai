class DwarfAI
    attr_accessor :onupdate_handle
    attr_accessor :update_counter

    attr_accessor :plan
    attr_accessor :citizen

    def initialize
        @citizen = {}
        @plan = Plan.new(self)

        @plan.setup_blueprint
    end


    def update
        @update_counter += 1
        update_citizenlist
        @plan.update
        update_jobs
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

    def update_jobs
        if update_counter % 10 == 1
            df.world.job_list.each { |j|
                j.flags.suspend = false if j.flags.suspend and not j.flags.repeat
                # TODO auto-labor style management
            }
        end
        if update_counter % 10 == 0
            update_nobles
        end
    end

    def update_nobles
        ent = df.ui.main.fortress_entity
        if not ent.positions.assignments.find { |a| a.histfig != -1 and ent.positions.own.binsearch(a.position_id).responsibilities[:MANAGE_PRODUCTION] }
            # TODO do not hardcode position name, check population caps, ...
            assign = ent.positions.assignments.find { |a| ent.positions.own.binsearch(a.position_id).code == 'MANAGER' }
            # TODO find a better candidate
            tg = df.unit_citizens.first
            office = @plan.ensure_workshop(:ManagersOffice)
            @plan.set_owner(office, tg.id)

            pos = DFHack::HistfigEntityLinkPositionst.cpp_new(:link_strength => 100, :start_year => df.cur_year)
            pos.entity_id = ent.id
            pos.assignment_id = assign.id
            tg.hist_figure_tg.entity_links << pos
            assign.histfig = tg.hist_figure_id

            ent.unknown2.unk6[4] << assign      # XXX wtf?

            df.add_announcement("AI: new manager: #{tg.name.to_s(false)}", 7, false) { |ann| ann.pos = tg.pos }
        end

        if not ent.positions.assignments.find { |a| a.histfig != -1 and ent.positions.own.binsearch(a.position_id).responsibilities[:ACCOUNTING] }
            assign = ent.positions.assignments.find { |a| ent.positions.own.binsearch(a.position_id).code == 'BOOKKEEPER' }
            tg = df.unit_citizens.find { |c| df.unit_entitypositions(c).find { |p| p.responsibilities[:MANAGE_PRODUCTION] } }
            office = @plan.ensure_workshop(:BookkeepersOffice)
            @plan.set_owner(office, tg.id)

            pos = DFHack::HistfigEntityLinkPositionst.cpp_new(:link_strength => 100, :start_year => df.cur_year)
            pos.entity_id = ent.id
            pos.assignment_id = assign.id
            tg.hist_figure_tg.entity_links << pos
            assign.histfig = tg.hist_figure_id

            ent.unknown2.unk6[6] << assign      # XXX wtf?

            df.ui.bookkeeper_settings = 4
            df.add_announcement("AI: new bookkeeper: #{tg.name.to_s(false)}", 7, false) { |ann| ann.pos = tg.pos }
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


    def onupdate_register
        @update_counter = 0
        @onupdate_handle = df.onupdate_register(120) { update }
        df.onstatechange_register { |st| statechanged(st) }
    end

    def onupdate_unregister
        df.onupdate_unregister(@onupdate_handle)
    end
end
