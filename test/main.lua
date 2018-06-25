function set_test_stage(stage)
    local f = io.open('test_stage.txt', 'w')
    f:write(stage)
    f:close()
end

print('running tests')

local output, status = dfhack.run_command_silent('ai', 'validate')
if status ~= CR_OK then
    print('ai validate failed!')
    print(output)
    set_test_stage('fail')
    dfhack.run_command('die')
end

set_test_stage('done')
dfhack.run_command('die')
