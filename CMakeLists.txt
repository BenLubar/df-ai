PROJECT (df-ai)

execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=8 --long
    WORKING_DIRECTORY "${df-ai_SOURCE_DIR}"
    OUTPUT_VARIABLE DF_AI_GIT_DESCRIPTION)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
    WORKING_DIRECTORY "${df-ai_SOURCE_DIR}"
    OUTPUT_VARIABLE DF_AI_GIT_COMMIT)
execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=8 --exact-match
    WORKING_DIRECTORY "${df-ai_SOURCE_DIR}"
    RESULT_VARIABLE DF_AI_GIT_TAGGED_RESULT
    OUTPUT_QUIET ERROR_QUIET)

set(git_describe_tmp_h ${df-ai_SOURCE_DIR}/df-ai-git-describe.tmp.h)
set(git_describe_h ${df-ai_SOURCE_DIR}/df-ai-git-describe.h)

file(WRITE ${git_describe_tmp_h} "")

macro(git_describe_definition var)
    string(STRIP "${${var}}" GIT_DEF_VALUE)
    file(APPEND ${git_describe_tmp_h} "#define ${var} \"${GIT_DEF_VALUE}\"\n")
endmacro()

git_describe_definition(DF_AI_GIT_DESCRIPTION)
git_describe_definition(DF_AI_GIT_COMMIT)
if(${DF_AI_GIT_TAGGED_RESULT} EQUAL 0)
    file(APPEND ${git_describe_tmp_h} "#define DF_AI_GIT_TAGGED\n")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${git_describe_tmp_h} ${git_describe_h})

SET(PROJECT_SRCS
    df-ai.cpp
    ai.cpp
    config.cpp
    hooks.cpp
    population.cpp
    plan.cpp
    plan_blueprint.cpp
    stocks.cpp
    camera.cpp
    embark.cpp
    room.cpp
    trade.cpp
    event_manager.cpp
)

SET(PROJECT_HDRS
    ai.h
    config.h
    hooks.h
    population.h
    plan.h
    plan_blueprint.h
    stocks.h
    camera.h
    embark.h
    room.h
    trade.h
    event_manager.h
    dfhack_shared.h
    df-ai-git-describe.h
)

ADD_LIBRARY(df-ai-biome STATIC thirdparty/exportmaps/cpp/df_utils/biome_type.cpp thirdparty/exportmaps/cpp/util/ofsub.cpp)
TARGET_LINK_LIBRARIES(df-ai-biome dfhack)

SET_SOURCE_FILES_PROPERTIES( ${PROJECT_HDRS} PROPERTIES HEADER_FILE_ONLY TRUE)

LIST(APPEND PROJECT_SRCS ${PROJECT_HDRS})

LIST(APPEND PROJECT_LIBS jsoncpp ${ZLIB_LIBRARIES} df-ai-biome)

IF(APPLE)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -undefined dynamic_lookup")
ENDIF()

DFHACK_PLUGIN(df-ai ${PROJECT_SRCS} LINK_LIBRARIES ${PROJECT_LIBS} COMPILE_FLAGS_GCC "-Wall -Wextra -Werror" COMPILE_FLAGS_MSVC "/W3")
