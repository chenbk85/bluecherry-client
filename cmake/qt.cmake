#
# Copyright 2010-2013 Bluecherry
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#

set (QT_USE_QTCORE 1)
set (QT_USE_QTDECLARATIVE 1)
set (QT_USE_QTGUI 1)
set (QT_USE_QTNETWORK 1)
set (QT_USE_QTOPENGL 1)
set (QT_USE_QTWEBKIT 1)

if (WIN32)
    set (QT_USE_QTMAIN 1)
endif (WIN32)

if (BUILD_TESTING)
    set (QT_USE_QTTEST 1)
endif (BUILD_TESTING)

find_package (Qt4 4.8.0 REQUIRED)
include (${QT_USE_FILE})

add_definitions (-DQT_NO_CAST_FROM_ASCII)
add_definitions (-DQT_NO_CAST_TO_ASCII)

list (APPEND bluecherry_client_LIBRARIES
    ${QT_LIBRARIES}
)

if (WIN32)
    list (APPEND bluecherry_client_LIBRARIES
        opengl32.lib
        glu32.lib
    )
endif (WIN32)
