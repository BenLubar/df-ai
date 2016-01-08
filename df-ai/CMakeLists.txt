PROJECT (df-ai)

SET(PROJECT_SRCS
    df-ai.cpp
    ai.cpp
    population.cpp
    plan.cpp
    stocks.cpp
    camera.cpp
    embark.cpp
    event_manager.cpp
)

SET(PROJECT_HDRS
    ai.h
    population.h
    plan.h
    stocks.h
    camera.h
    embark.h
    room.h
    event_manager.h
    horrible.h
    dfhack_shared.h
)

SET_SOURCE_FILES_PROPERTIES( ${PROJECT_HDRS} PROPERTIES HEADER_FILE_ONLY TRUE)

LIST(APPEND PROJECT_SRCS ${PROJECT_HDRS})

DFHACK_PLUGIN(df-ai ${PROJECT_SRCS} LINK_LIBRARIES ${PROJECT_LIBS} COMPILE_FLAGS -Wall -Wextra -Werror -pedantic)

# vim: et:sw=4:ts=4
