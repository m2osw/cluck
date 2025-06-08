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

project(cluckd)

add_executable(${PROJECT_NAME}
    cluckd.cpp
    computer.cpp
    interrupt.cpp
    main.cpp
    messenger.cpp
    ticket.cpp
    timer.cpp
)

if(${CLUCKD_OPTIMIZATIONS})
    target_compile_definitions(${PROJECT_NAME}
        PUBLIC
            CLUCKD_OPTIMIZATIONS
    )
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${ADVGETOPT_INCLUDE_DIRS}
        ${AS2JS_INCLUDE_DIRS}
        ${CPPTHREAD_INCLUDE_DIRS}
        ${EVENTDISPATCHER_INCLUDE_DIRS}
        ${LIBADDR_INCLUDE_DIRS}
        ${LIBEXCEPT_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
        ${SNAPLOGGER_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    cluck
    ${ADVGETOPT_LIBRARIES}
    ${AS2JS_LIBRARIES}
    ${CPPTHREAD_LIBRARIES}
    ${LIBADDR_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

install(
    TARGETS
        ${PROJECT_NAME}

    RUNTIME DESTINATION
        sbin
)

add_subdirectory(message-definitions)

# vim: ts=4 sw=4 et
