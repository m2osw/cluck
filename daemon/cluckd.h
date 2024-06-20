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
#include    "computer.h"
#include    "interrupt.h"
#include    "message_cache.h"
#include    "ticket.h"
#include    "timer.h"


// eventdispatcher
//
#include    <eventdispatcher/message.h>



namespace cluck_daemon
{



//#pragma GCC diagnostic push
//#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
class cluckd
    //: public snap::snap_communicator::connection_with_send_message
    //, public std::enable_shared_from_this<cluckd>
{
public:
    typedef std::shared_ptr<cluckd>      pointer_t;

    //static std::int64_t const   DEFAULT_TIMEOUT = 5; // in seconds
    //static std::int64_t const   MIN_TIMEOUT     = 3; // in seconds

                                cluckd(int argc, char * argv[]);
                                cluckd(cluckd const & rhs) = delete;
    virtual                     ~cluckd();

    cluckd &                    operator = (cluckd const & rhs) = delete;

    void                        add_connections();
    void                        run();
    //void                        process_message(ed::message const & msg);
    void                        tool_message(ed::message const & msg);
    void                        process_connection(int const s);

    // connection_with_send_message implementation; use messenger
    //
    //virtual bool                send_message(ed::message const & msg, bool cache = false) override;

    int                         get_computer_count() const;
    std::string const &         get_server_name() const;
    bool                        is_daemon_ready() const;
    computer::pointer_t         is_leader(std::string id = std::string()) const;
    computer::pointer_t         get_leader_a() const;
    computer::pointer_t         get_leader_b() const;
    void                        cleanup();
    ticket::ticket_id_t         get_last_ticket(std::string const & lock_name);
    void                        set_ticket(std::string const & object_name, std::string const & key, ticket::pointer_t ticket);
    void                        lock_exiting(ed::message & msg);
    ticket::key_map_t const     get_entering_tickets(std::string const & lock_name);
    std::string                 serialized_tickets();
    ticket::pointer_t           find_first_lock(std::string const & lock_name);
    void                        stop(bool quitting);
    std::string                 ticket_list() const;
    void                        send_lock_started(ed::message const * msg);
    void                        election_status();

    // messages received by the messenger which then calls the cluckd functions
    // however, the messenger accesses all of them to setup the dispatcher
    //
    void                        msg_absolutely(ed::message & msg);
    void                        msg_activate_lock(ed::message & msg);
    void                        msg_add_ticket(ed::message & msg);
    void                        msg_cluster_up(ed::message & msg);
    void                        msg_cluster_down(ed::message & msg);
    void                        msg_drop_ticket(ed::message & msg);
    void                        msg_get_max_ticket(ed::message & msg);
    void                        msg_info(ed::message & msg);
    void                        msg_list_tickets(ed::message & msg);
    void                        msg_lock(ed::message & msg);
    void                        msg_lock_activated(ed::message & msg);
    void                        msg_lock_entered(ed::message & msg);
    void                        msg_lock_entering(ed::message & msg);
    void                        msg_lock_exiting(ed::message & msg);
    void                        msg_lock_failed(ed::message & msg);
    void                        msg_lock_leaders(ed::message & msg);
    void                        msg_lock_started(ed::message & msg);
    void                        msg_lock_status(ed::message & msg);
    void                        msg_lock_tickets(ed::message & msg);
    void                        msg_max_ticket(ed::message & msg);
    void                        msg_server_gone(ed::message & msg);
    void                        msg_status(ed::message & msg);
    void                        msg_ticket_added(ed::message & msg);
    void                        msg_ticket_ready(ed::message & msg);
    void                        msg_unlock(ed::message & msg);

private:
    bool                        get_parameters(
                                      ed::message const & message
                                    , std::string * object_name
                                    , ed::dispatcher_match::tag_t * tag
                                    , pid_t * client_pid
                                    , cluck::timeout_t * timeout
                                    , std::string * key
                                    , std::string * source);
    void                        activate_first_lock(std::string const & object_name);
    void                        check_lock_status();
    void                        synchronize_leaders();
    void                        forward_message_to_leader(ed::message & message);

    //static ed::dispatcher<cluckd>::dispatcher_match::vector_t const    g_snaplock_service_messages;

    advgetopt::getopt                   f_opts;
    //snap::snap_config                   f_config;

    cluck::timeout_t                    f_start_time = cluck::timeout_t();
    std::string                         f_server_name = std::string();
    ed::communicator::pointer_t         f_communicator = ed::communicator::pointer_t();
//    std::string                         f_host_list = std::string("localhost");
    messenger::pointer_t                f_messenger = messenger::pointer_t();
    interrupt::pointer_t                f_interrupt = interrupt::pointer_t();
    timer::pointer_t                    f_timer = timer::pointer_t();
//    bool                                f_stop_received = false;
//    bool                                f_debug = false;
//    bool                                f_debug_lock_messages = false;
    std::size_t                         f_neighbors_count = 0;
    std::size_t                         f_neighbors_quorum = 0;
    std::string                         f_my_id = std::string();
    addr::addr                          f_my_ip_address = addr::addr();
    bool                                f_lock_status = false;                  // not ready
    computer::map_t                     f_computers = computer::map_t();        // key is the computer name
    computer::vector_t                  f_leaders = computer::vector_t();
    int                                 f_next_leader = 0;
    message_cache::list_t               f_message_cache = message_cache::list_t();
    ticket::object_map_t                f_entering_tickets = ticket::object_map_t();
    ticket::object_map_t                f_tickets = ticket::object_map_t();
    snapdev::timespec_ex                f_election_date = snapdev::timespec_ex();
    ticket::serial_t                    f_ticket_serial = 0;
    mutable time_t                      f_pace_lockstarted = 0;
};
//#pragma GCC diagnostic pop


} // namespace cluck_daemon
// vim: ts=4 sw=4 et
