PROJECT (df-ai)

SET(PROJECT_SRCS
    df-ai.cpp
    ai.cpp
    population.cpp
    plan.cpp
    stocks.cpp
    camera.cpp
    embark.cpp
)

SET(PROJECT_HDRS
    ai.h
)

SET_SOURCE_FILES_PROPERTIES( ${PROJECT_HDRS} PROPERTIES HEADER_FILE_ONLY TRUE)

LIST(APPEND PROJECT_SRCS ${PROJECT_HDRS})

DFHACK_PLUGIN(df-ai ${PROJECT_SRCS} LINK_LIBRARIES ${PROJECT_LIBS})

# vim: et:sw=4:ts=4
