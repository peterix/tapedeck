set( QT_USE_QTGUI TRUE )
set( QT_USE_QTMULTIMEDIA TRUE )
INCLUDE( ${QT_USE_FILE} )

set ( mul_SRCS
    mul.cpp
    main.cpp
)

SET ( mul_UI
    gui/main.ui
)

SET( mul_RCS
    gui/resources.qrc
)

# this command will generate rules that will run rcc on all files from blankslade_RCS
# in result blankslade_RC_SRCS variable will contain paths to files produced by rcc
QT4_ADD_RESOURCES( mul_RC_SRCS ${mul_RCS} )

QT4_WRAP_UI(mul_UI_h ${mul_UI})
qt4_automoc(${mul_SRCS})
include_directories(${mul_BINARY_DIR}/src/)
include_directories( ${SNDFILE_INCLUDE_DIR})
include_directories(${Lame_INCLUDE_DIR})
add_executable(mul ${mul_SRCS} ${mul_RC_SRCS} ${mul_UI_h})
target_link_libraries(mul ${QT_LIBRARIES} ${SNDFILE_LIBRARY} ${Lame_LIBRARIES})