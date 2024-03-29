// Copyright (c) 2016-2024  Made to Order Software Corp.  All Rights Reserved
//
// https://snapwebsites.org/
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

// eventdispatcher
//
#include    <eventdispatcher/signal.h>



namespace cluck_daemon
{



class cluckd;

class debug_info
    : public ed::signal
{
public:
    typedef std::shared_ptr<debug_info>     pointer_t;

                            debug_info(cluckd * c);
                            debug_info(debug_info const &) = delete;
    virtual                 ~debug_info() override {}

    debug_info const &      operator = (debug_info const &) = delete;

    // ed::connection implementation
    virtual void            process_signal() override;

private:
    cluckd *                f_cluckd = nullptr;
};



} // namespace cluck_deamon
// vim: ts=4 sw=4 et
