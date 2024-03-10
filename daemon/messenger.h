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
//#include    "computer.h"
//#include    "debug_info.h"
//#include    "info.h"
//#include    "interrupt.h"
//#include    "message_cache.h"
//#include    "ticket.h"
//#include    "timer.h"


// cluck
//
//#include    <cluck/cluck.h>


// fluid-settings
//
#include    <fluid-settings/fluid_settings_connection.h>



namespace cluck_daemon
{



class cluckd;


class messenger
    : public fluid_settings::fluid_settings_connection
{
public:
    typedef std::shared_ptr<messenger>     pointer_t;

                                messenger(cluckd * c, advgetopt::getopt & opts);
                                messenger(messenger const &) = delete;
    virtual                     ~messenger() override {}

    messenger &                 operator = (messenger const &) = delete;

    // fluid_settings::fluid_settings_connection()
    virtual void                fluid_settings_changed(
                                      fluid_settings::fluid_settings_status_t status
                                    , std::string const & name
                                    , std::string const & value) override;

private:
    //void                        get_parameters(
    //                                  ed::message const & message
    //                                , std::string * object_name
    //                                , pid_t * client_pid
    //                                , cluck::timeout_t * timeout
    //                                , std::string * key
    //                                , std::string * source);
    //void                        activate_first_lock(std::string const & object_name);
    //void                        check_lock_status();
    //void                        synchronize_leaders();
    //void                        forward_message_to_leader(ed::message & message);

    //void                        msg_absolutely(ed::message & msg);
    //void                        msg_activate_lock(ed::message & msg);
    //void                        msg_add_ticket(ed::message & msg);
    //void                        msg_cluster_down(ed::message & msg);
    //void                        msg_cluster_up(ed::message & msg);
    //void                        msg_drop_ticket(ed::message & msg);
    //void                        msg_get_max_ticket(ed::message & msg);
    //void                        msg_list_tickets(ed::message & msg);
    //void                        msg_lock(ed::message & msg);
    //void                        msg_lock_activated(ed::message & msg);
    //void                        msg_lock_entered(ed::message & msg);
    //void                        msg_lock_entering(ed::message & msg);
    //void                        msg_lock_exiting(ed::message & msg);
    //void                        msg_lock_failed(ed::message & msg);
    //void                        msg_lock_leaders(ed::message & msg);
    //void                        msg_lock_started(ed::message & msg);
    //void                        msg_lock_status(ed::message & msg);
    //void                        msg_lock_tickets(ed::message & msg);
    //void                        msg_max_ticket(ed::message & msg);
    //void                        msg_server_gone(ed::message & msg);
    //void                        msg_status(ed::message & msg);
    //void                        msg_ticket_added(ed::message & msg);
    //void                        msg_ticket_ready(ed::message & msg);
    //void                        msg_unlock(ed::message & msg);

    cluckd *                    f_cluckd = nullptr;
    ed::dispatcher::pointer_t   f_dispatcher = ed::dispatcher::pointer_t();

    //time_t                              f_start_time = -1;
    //std::string                         f_server_name = std::string();
    //std::string                         f_service_name = std::string("cluckd");
    //std::string                         f_communicator_addr = std::string("localhost");
    //int                                 f_communicator_port = 4040;
    //ed::communicator::pointer_t f_communicator = ed::communicator::pointer_t();
    //std::string                         f_host_list = std::string("localhost");
    //messenger::pointer_t                f_messenger = messenger::pointer_t();
    //interrupt::pointer_t                f_interrupt = interrupt::pointer_t();
    //timer::pointer_t                    f_timer = timer::pointer_t();
    //info::pointer_t                     f_info = info::pointer_t();
    //debug_info::pointer_t               f_debug_info = debug_info::pointer_t();
    //bool                                f_stop_received = false;
    //bool                                f_debug = false;
    //bool                                f_debug_lock_messages = false;
    //std::size_t                         f_neighbors_count = 0;
    //std::size_t                         f_neighbors_quorum = 0;
    //std::string                         f_my_id = std::string();
    //std::string                         f_my_ip_address = std::string();
    //bool                                f_lock_status = false;                  // not ready
    //computer::map_t                     f_computers = computer::map_t();        // key is the computer name
    //computer::vector_t                  f_leaders = computer::vector_t();
    //int                                 f_next_leader = 0;
    //message_cache::list_t               f_message_cache = message_cache::list_t();
    //ticket::object_map_t                f_entering_tickets = ticket::object_map_t();
    //ticket::object_map_t                f_tickets = ticket::object_map_t();
    //snapdev::timespec_ex                f_election_date = snapdev::timespec_ex();
    //ticket::serial_t                    f_ticket_serial = 0;
    //mutable time_t                      f_pace_lockstarted = 0;
};



} // namespace cluck_deamon
// vim: ts=4 sw=4 et
