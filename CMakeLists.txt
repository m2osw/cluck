# Copyright (c) 2016-2025  Made to Order Software Corp.  All Rights Reserved
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

cmake_minimum_required(VERSION 3.10.2)

project(cluck_project)

find_package(AdvGetOpt                REQUIRED)
find_package(As2Js                    REQUIRED)
find_package(AtomicNames              REQUIRED)
find_package(Communicator             REQUIRED)
find_package(CppThread                REQUIRED)
find_package(EventDispatcher          REQUIRED)
find_package(FluidSettings            REQUIRED)
find_package(LibAddr                  REQUIRED)
find_package(LibExcept                REQUIRED)
find_package(OpenSSL                  REQUIRED)
find_package(SnapCMakeModules         REQUIRED)
find_package(SnapDev                  REQUIRED)
find_package(SnapLogger               REQUIRED)
find_package(VerifyMessageDefinitions REQUIRED)

SnapGetVersion(CLUCK ${CMAKE_CURRENT_SOURCE_DIR})

enable_language(CXX)

option(CLUCKD_OPTIMIZATIONS "Whether to optimize the cluck daemon as much as possible (i.e. avoid additional human descriptions)." OFF)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(cluck)
add_subdirectory(daemon)
add_subdirectory(tools)
add_subdirectory(cmake)
add_subdirectory(tests)
add_subdirectory(conf)
add_subdirectory(doc)

# vim: ts=4 sw=4 et
