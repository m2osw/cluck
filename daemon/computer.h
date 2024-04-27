// Copyright (c) 2016-2024  Made to Order Software Corp.  All Rights Reserved
//
// https://snapwebsites.org/project/cluck
// contact@m2osw.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
#pragma once

// libaddr
//
#include    <libaddr/addr.h>


// snapdev
//
#include    <snapdev/timespec_ex.h>


// C++
//
#include    <map>
#include    <memory>
#include    <string>
#include    <vector>



namespace cluck_daemon
{



class computer
{
public:
    typedef std::int8_t                         priority_t;
    typedef std::uint32_t                       random_t;
    typedef std::shared_ptr<computer>           pointer_t;
    typedef std::map<std::string, pointer_t>    map_t;
    typedef std::vector<pointer_t>              vector_t;

    static priority_t const PRIORITY_UNDEFINED = -1;
    static priority_t const PRIORITY_MIN = 0;
    static priority_t const PRIORITY_LEADER = 0;
    static priority_t const PRIORITY_USER_MIN = 1;
    static priority_t const PRIORITY_DEFAULT = 14;
    static priority_t const PRIORITY_OFF = 15;
    static priority_t const PRIORITY_MAX = 15;

                            computer();
                            computer(
                                  std::string const & name
                                , priority_t priority
                                , addr::addr ip_address);

    bool                    is_self() const;
    void                    set_connected(bool connected);
    bool                    get_connected() const;
    bool                    set_id(std::string const & id);
    priority_t              get_priority() const;
    void                    set_start_time(snapdev::timespec_ex const & start_time);
    snapdev::timespec_ex const &
                            get_start_time() const;

    std::string const &     get_name() const;
    std::string const &     get_id() const;
    addr::addr const &      get_ip_address() const;

private:
    mutable std::string     f_id = std::string();

    bool                    f_connected = true;
    bool                    f_self = false;

    priority_t              f_priority = PRIORITY_UNDEFINED;
    random_t                f_random_id = 0;
    addr::addr              f_ip_address = addr::addr();
    pid_t                   f_pid = 0;
    std::string             f_name = std::string();

    snapdev::timespec_ex    f_start_time = snapdev::timespec_ex();
};



} // namespace cluck_daemon
// vim: ts=4 sw=4 et
