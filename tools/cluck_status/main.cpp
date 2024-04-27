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


// self
//
#include    "server.h"


// cluck
//
#include    <cluck/exception.h>


// snaplogger
//
#include    <snaplogger/message.h>


// eventdispatcher
//
#include    <eventdispatcher/signal_handler.h>


// libexcept
//
#include    <libexcept/file_inheritance.h>


// advgetopt
//
#include <advgetopt/exception.h>


// last include
//
#include <snapdev/poison.h>



int main(int argc, char * argv[])
{
    ed::signal_handler::create_instance();
    libexcept::verify_inherited_files();
    libexcept::collect_stack_trace();

    std::string errmsg;
    try
    {
        lock_status::server::pointer_t s(std::make_shared<lock_status::server>(argc, argv));
        return s->run();
    }
    catch(advgetopt::getopt_exit const & e)
    {
        std::cerr << "error: " << e.what() << std::endl;
        return e.code();
    }
    catch(std::exception const & e)
    {
        errmsg = "cluckd: std::exception caught! ";
        errmsg += e.what();
    }
    catch(...)
    {
        errmsg = "cluckd: unknown exception caught!";
    }

    SNAP_LOG_FATAL << errmsg << SNAP_LOG_SEND;
    if(isatty(STDERR_FILENO))
    {
        std::cerr << errmsg << std::endl;
    }

    return 1;
}



// vim: ts=4 sw=4 et
