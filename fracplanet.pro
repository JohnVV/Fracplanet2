TARGET = Fracplanet
TEMPLATE = app

CONFIG += qt stl exceptions release c++17  # debug/release
QT += opengl openglwidgets

HEADERS += $$files(*.h)
SOURCES += $$files(*.cpp)
LIBS += -lboost_program_options

DEFINES += QT_DLL

#######################################
# Version numbering.  VERSION_NUMBER=x.x.x should be set on qmake arguments.

QMAKE_CXXFLAGS_RELEASE += '-DFRACPLANET_VERSION="$$VERSION_NUMBER"'
QMAKE_CXXFLAGS_DEBUG   += '-DFRACPLANET_VERSION="$$VERSION_NUMBER"'
QMAKE_CXXFLAGS_RELEASE += '-DFRACPLANET_BUILD="$$VERSION_NUMBER (release build)"'
QMAKE_CXXFLAGS_DEBUG   += '-DFRACPLANET_BUILD="$$VERSION_NUMBER (debug build)"'

VERSION=$$VERSION_NUMBER

#######################################
# Disable assertions in release version

QMAKE_CXXFLAGS_RELEASE += -DNDEBUG
QMAKE_CFLAGS_RELEASE += -DNDEBUG

######################################
# Disable implicit cast from QString to char*

QMAKE_CXXFLAGS_RELEASE += -DQT_NO_ASCII_CAST
QMAKE_CXXFLAGS_DEBUG += -DQT_NO_ASCII_CAST

######################################
# Pick up any dpkg-buildflags flags via environment, release only

QMAKE_CXXFLAGS_RELEASE += $$(CPPFLAGS) $$(CXXFLAGS)
QMAKE_CFLAGS_RELEASE += $$(CPPFLAGS) $$(CFLAGS)
QMAKE_LFLAGS += $$(LDFLAGS)

######################################
# Hide those crufty moc_ files away

MOC_DIR = moc
OBJECTS_DIR = obj
