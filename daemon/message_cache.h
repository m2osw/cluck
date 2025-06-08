// Copyright (c) 2016-2025  Made to Order Software Corp.  All Rights Reserved
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

// cluck
//
#include    <cluck/cluck.h>


// eventdispatcher
//
#include    <eventdispatcher/message.h>



namespace cluck_daemon
{



struct message_cache
{
    typedef std::list<message_cache>  list_t;

    cluck::timeout_t        f_timeout = cluck::timeout_t();
    ed::message             f_message = ed::message();
};



} // namespace cluck_daemon
// vim: ts=4 sw=4 et
