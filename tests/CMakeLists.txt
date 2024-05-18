# Copyright (c) 2016-2024  Made to Order Software Corp.  All Rights Reserved
#
# https://snapwebsites.org/project/cluck
# contact@m2osw.com
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

##
## cluck unittest
##
project(unittest)

find_package(SnapCatch2)
find_package(Reporter)

if(SnapCatch2_FOUND AND Reporter_FOUND)

    add_executable(${PROJECT_NAME}
        catch_main.cpp

        catch_cluck.cpp
        catch_version.cpp
    )

    target_include_directories(${PROJECT_NAME}
        PUBLIC
            ${CMAKE_BINARY_DIR}
            ${PROJECT_SOURCE_DIR}

            ${ADVGETOPT_INCLUDE_DIRS}
            ${LIBADDR_INCLUDE_DIRS}
            ${LIBEXCEPT_INCLUDE_DIRS}
            ${REPORTER_SOURCE_DIR}
            ${SNAPCATCH2_INCLUDE_DIRS}
            ${SNAPDEV_INCLUDE_DIRS}
    )

    target_link_libraries(${PROJECT_NAME}
        cluck
        ${ADVGETOPT_LIBRARIES}
        ${LIBADDR_LIBRARIES}
        ${LIBEXCEPT_LIBRARIES}
        ${REPORTER_LIBRARIES}
        ${SNAPCATCH2_LIBRARIES}
    )

    add_test(unittests ${PROJECT_NAME})

else()

    message("snapcatch2 and/or reporter not found... no tests will be built.")

endif()


# vim: ts=4 sw=4 et
