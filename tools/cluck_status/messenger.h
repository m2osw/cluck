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

// fluid-settings
//
#include    <fluid-settings/fluid_settings_connection.h>



namespace lock_status
{



class server;


class messenger
    : public fluid_settings::fluid_settings_connection
{
public:
    typedef std::shared_ptr<messenger>      pointer_t;

                                messenger(server * l, advgetopt::getopt & opts);
                                messenger(messenger const &) = delete;
    virtual                     ~messenger() override {}

    messenger &                 operator = (messenger const &) = delete;

    // fluid_settings::fluid_settings_connection()
    virtual void                fluid_settings_changed(
                                      fluid_settings::fluid_settings_status_t status
                                    , std::string const & name
                                    , std::string const & value) override;

private:
    void                        msg_lock_ready(ed::message & msg);
    void                        msg_no_lock(ed::message & msg);
    void                        msg_ticket_list(ed::message & msg);
    void                        msg_transmission_report(ed::message & msg);

    server *                    f_server = nullptr;
    ed::dispatcher::pointer_t   f_dispatcher = ed::dispatcher::pointer_t();
    bool                        f_quiet = false;
    std::string                 f_server_name = std::string();
    std::string                 f_command = std::string();
};



} // namespace lock_status
// vim: ts=4 sw=4 et
