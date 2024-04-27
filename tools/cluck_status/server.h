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

// self
//
#include    "messenger.h"



namespace lock_status
{



class server
{
public:
    typedef std::shared_ptr<server>  pointer_t;

                                server(int argc, char * argv[]);
                                server(server const &) = delete;
    virtual                     ~server() {}

    server &                    operator = (server const &) = delete;

    int                         run();

private:
    advgetopt::getopt           f_opts;
    ed::communicator::pointer_t f_communicator = ed::communicator::pointer_t();
    messenger::pointer_t        f_messenger = messenger::pointer_t();
};



} // namespace lock_status
// vim: ts=4 sw=4 et
