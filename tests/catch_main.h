// Copyright (c) 2016-2025  Made to Order Software Corp.  All Rights Reserved
//
// https://snapwebsites.org/project/eventdispatcher
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

// catch2
//
#include    <snaplogger/snapcatch2.hpp>


// cppthread
//
#include    <cppthread/log.h>


// C++
//
#include    <string>
#include    <cstring>
#include    <cstdlib>
#include    <iostream>



namespace SNAP_CATCH2_NAMESPACE
{



//extern char ** g_argv;



class reset_log_errors
{
public:
    ~reset_log_errors()
    {
std::cerr << "--- errors before reset: " << cppthread::log.get_errors() << "\n";
        cppthread::log.reset_counters();
std::cerr << "--- errors after reset: " << cppthread::log.get_errors() << "\n";
    }
};



}
// namespace SNAP_CATCH2_NAMESPACE
// vim: ts=4 sw=4 et
