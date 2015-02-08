# a dwarf fortress autonomous artificial intelligence (more or less)

case $script_args[0]
when 'start'
    Dir['hack/scripts/ai/*.rb'].each { |f| load f }

    if df.curview._raw_rtti_classname == 'viewscreen_titlest' and not $AI_RANDOM_EMBARK
        df.curview.feed_keys(:SELECT)
        df.curview.feed_keys(:SELECT)
    end

    $dwarfAI = DwarfAI.new

    df.onupdate_register_once('df-ai start') {
        view = df.curview
        if view._raw_rtti_classname == 'viewscreen_dwarfmodest'
            begin
                $dwarfAI.onupdate_register
                $dwarfAI.startup
                view.feed_keys(:D_PAUSE) if df.pause_state
            rescue Exception
                $dwarfAI.debug $!
                $dwarfAI.debug $!.backtrace.join("\n")
                $dwarfAI.abandon!(view)
            end
            true
        end
    }

when 'end', 'stop'
    $dwarfAI.onupdate_unregister
    puts "removed onupdate"

when 'patch', 'update'
    Dir['hack/scripts/ai/*.rb'].each { |f| load f }

else
    if $dwarfAI
        puts $dwarfAI.status
    else
        puts "AI not started (hint: ai start)"
    end
end

# vim: et:sw=4:ts=4
