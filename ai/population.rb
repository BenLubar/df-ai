class DwarfAI
    class Population
        class Citizen
            attr_accessor :id, :role, :idlecounter
            def initialize(id)
                @id = id
                @idlecounter = 0
            end

            def dfunit
                df.unit_find(@id)
            end
        end

        attr_accessor :ai, :citizen
        def initialize(ai)
            @ai = ai
            @citizen = {}
        end

        def update
            case ai.update_counter % 10
            when 0; update_nobles
            when 1; update_citizenlist
            when 2; update_jobs
            end
        end

        def new_citizen(id)
            c = Citizen.new(id)
            @citizen[id] = c
            ai.plan.new_citizen(id)
            c
        end

        def del_citizen(id)
            @citizen.delete id
            ai.plan.del_citizen(id)
            # TODO reassign jobs (noble/labors)
        end

        def update_citizenlist
            old = @citizen.dup

            # add new fort citizen to our list
            # TODO check berserk ?
            df.unit_citizens.each { |u|
                @citizen[u.id] ||= new_citizen(u.id)
                old.delete u.id
            }

            # del those who are no longer here
            old.each { |id, c|
                del_citizen(id)
            }
        end

        def update_jobs
            df.world.job_list.each { |j|
                j.flags.suspend = false if j.flags.suspend and not j.flags.repeat
                # TODO auto-labor style management
            }
        end

        def unit_totalxp(u)
            u.status.current_soul.skills.inject(0) { |t, sk|
                rat = DFHack::SkillRating.int(sk.rating)
                t + 400*rat + 100*rat*(rat+1)/2 + sk.experience
            }
        end

        def update_nobles
            ent = df.ui.main.fortress_entity
            if not ent.positions.assignments.find { |a| a.histfig != -1 and ent.positions.own.binsearch(a.position_id).responsibilities[:MANAGE_PRODUCTION] }
                # TODO do not hardcode position name, check population caps, ...
                assign = ent.positions.assignments.find { |a| ent.positions.own.binsearch(a.position_id).code == 'MANAGER' }
                # TODO find a better candidate
                tg = df.unit_citizens.sort_by { |u| unit_totalxp(u) }.first
                office = ai.plan.ensure_workshop(:ManagersOffice)
                ai.plan.set_owner(office, tg.id)

                pos = DFHack::HistfigEntityLinkPositionst.cpp_new(:link_strength => 100, :start_year => df.cur_year)
                pos.entity_id = ent.id
                pos.assignment_id = assign.id
                tg.hist_figure_tg.entity_links << pos
                assign.histfig = tg.hist_figure_id

                ent.assignments_by_type[:MANAGE_PRODUCTION] << assign

                df.add_announcement("AI: new manager: #{tg.name.to_s(false)}", 7, false) { |ann| ann.pos = tg.pos }
            end

            if not ent.positions.assignments.find { |a| a.histfig != -1 and ent.positions.own.binsearch(a.position_id).responsibilities[:ACCOUNTING] }
                assign = ent.positions.assignments.find { |a| ent.positions.own.binsearch(a.position_id).code == 'BOOKKEEPER' }
                tg = df.unit_citizens.find { |c| df.unit_entitypositions(c).find { |p| p.responsibilities[:MANAGE_PRODUCTION] } }
                office = ai.plan.ensure_workshop(:BookkeepersOffice)
                ai.plan.set_owner(office, tg.id)

                pos = DFHack::HistfigEntityLinkPositionst.cpp_new(:link_strength => 100, :start_year => df.cur_year)
                pos.entity_id = ent.id
                pos.assignment_id = assign.id
                tg.hist_figure_tg.entity_links << pos
                assign.histfig = tg.hist_figure_id

                ent.assignments_by_type[:ACCOUNTING] << assign

                df.ui.bookkeeper_settings = 4
                df.add_announcement("AI: new bookkeeper: #{tg.name.to_s(false)}", 7, false) { |ann| ann.pos = tg.pos }
            end
        end

        def status
            "#{@citizen.length} citizen"
        end
    end
end
