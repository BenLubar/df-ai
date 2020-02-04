function set_test_stage(stage)
    local f = io.open('test_stage.txt', 'w')
    f:write(stage)
    f:close()
end

print('Running tests')

print('Running file: main')
local status = dfhack.run_command('ai', 'validate')
if status == CR_OK then
    print('test passed: ai validate')
else
    dfhack.printerr('test errored: ai validate: status=' .. tostring(status))
end

set_test_stage('done')
dfhack.run_command('die')
