# a dwarf fortress autonomous artificial intelligence (more or less)

case $script_args[0]
when 'start'
    Dir['hack/scripts/ai/*.rb'].each { |f| load f }

    $dwarfAI = DwarfAI.new
    $dwarfAI.onupdate_register

    # unpause if a map was just loaded
    if df.pause_state and df.cur_year_tick <= 1
        u = df.onupdate_register {
            df.onupdate_unregister u
            df.curview.feed_keys(:D_PAUSE) if df.curview._raw_rtti_classname == 'viewscreen_dwarfmodest'
        }
    end
when 'end', 'stop'
    $dwarfAI.onupdate_unregister
    puts "removed onupdate"

when 'patch', 'update'
    Dir['hack/scripts/ai/*.rb'].each { |f| load f }

else
    if $dwarfAI
        puts $dwarfAI.status
    else
        puts "AI not started"
    end
end
