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

// self
//
#include    "messenger.h"


// cluck
//
#include    <cluck/cluck.h>


// fluid-settings
//
#include    <fluid-settings/fluid_settings_connection.h>



namespace cluck_daemon
{



class ticket
    : public std::enable_shared_from_this<ticket>
{
public:
    typedef std::shared_ptr<ticket>             pointer_t;
    typedef std::vector<pointer_t>              vector_t;
    typedef std::map<std::string, pointer_t>    key_map_t;      // sorted by key
    typedef std::map<std::string, key_map_t>    object_map_t;   // sorted by object_name
    typedef std::int32_t                        serial_t;
    typedef std::uint32_t                       ticket_id_t;

    static serial_t const                       NO_SERIAL = -1;
    static ticket_id_t const                    NO_TICKET = 0;

                                ticket(
                                          cluckd * c
                                        , messenger::pointer_t messenger
                                        , std::string const & lock_name
                                        , ed::dispatcher_match::tag_t tag
                                        , std::string const & entering_key
                                        , cluck::timeout_t obtention_timeout
                                        , cluck::timeout_t lock_duration
                                        , std::string const & server_name
                                        , std::string const & service_name);
                                ticket(ticket const &) = delete;
    ticket &                    operator = (ticket const &) = delete;

    // message handling
    //
    bool                        send_message_to_leaders(ed::message & msg);
    void                        entering();
    void                        entered();
    void                        max_ticket(ticket_id_t new_max_ticket);
    void                        add_ticket();
    void                        ticket_added(key_map_t const & entering);
    void                        remove_entering(std::string const & key);
    void                        activate_lock();
    void                        lock_activated();
    void                        drop_ticket(); // this is called when we receive the UNLOCK event
    void                        lock_failed(std::string const & reason);
    void                        lock_tickets();

    // object handling
    //
    void                        set_owner(std::string const & owner);
    std::string const &         get_owner() const;
    pid_t                       get_client_pid() const;
    void                        set_serial(serial_t owner);
    serial_t                    get_serial() const;
    void                        set_unlock_duration(cluck::timeout_t duration);
    cluck::timeout_t            get_unlock_duration() const;
    void                        set_ready();
    void                        set_ticket_number(ticket_id_t number);
    ticket_id_t                 get_ticket_number() const;
    bool                        is_locked() const;
    bool                        one_leader() const;
    cluck::timeout_t            get_obtention_timeout() const;
    void                        set_alive_timeout(cluck::timeout_t timeout);
    cluck::timeout_t            get_lock_duration() const;
    cluck::timeout_t            get_lock_timeout_date() const;
    cluck::timeout_t            get_current_timeout_date() const;
    bool                        timed_out() const;
    std::string const &         get_object_name() const;
    ed::dispatcher_match::tag_t get_tag() const;
    std::string const &         get_server_name() const;
    std::string const &         get_service_name() const;
    std::string const &         get_entering_key() const;
    std::string const &         get_ticket_key() const;
    std::string                 serialize() const;
    void                        unserialize(std::string const & data);

private:
    enum class lock_failure_t
    {
        LOCK_FAILURE_NONE,          // no failure so far
        LOCK_FAILURE_LOCK,          // LOCK timed out
        LOCK_FAILURE_UNLOCKING,     // UNLOCKING timed out
    };

    // this is owned by a cluckd object so no need for a smart pointer
    // (and it would create a parent/child loop)
    //
    cluckd *                        f_cluckd = nullptr;

    // initialization
    //
    messenger::pointer_t            f_messenger = messenger::pointer_t();
    std::string                     f_object_name = std::string();
    ed::dispatcher_match::tag_t     f_tag = ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG;
    cluck::timeout_t                f_obtention_timeout = cluck::timeout_t();
    cluck::timeout_t                f_alive_timeout = cluck::timeout_t();
    cluck::timeout_t                f_lock_duration = cluck::timeout_t();
    cluck::timeout_t                f_unlock_duration = cluck::timeout_t();
    std::string                     f_server_name = std::string();
    std::string                     f_service_name = std::string();
    std::string                     f_owner = std::string();
    serial_t                        f_serial = NO_SERIAL;

    // initialized, entering
    //
    std::string                     f_entering_key = std::string();
    bool                            f_get_max_ticket = false;

    // entered, adding ticket
    //
    ticket_id_t                     f_our_ticket = NO_TICKET;
    bool                            f_added_ticket = false;
    std::string                     f_ticket_key = std::string();

    // ticket added, exiting
    //
    bool                            f_added_ticket_quorum = false;
    key_map_t                       f_still_entering = key_map_t();

    // exited, ticket ready
    //
    bool                            f_ticket_ready = false;

    // locked
    //
    bool                            f_locked = false;
    cluck::timeout_t                f_lock_timeout_date = cluck::timeout_t();
    cluck::timeout_t                f_unlocked_timeout_date = cluck::timeout_t();

    // the lock did not take (in most cases, this is because of a timeout)
    //
    lock_failure_t                  f_lock_failed = lock_failure_t::LOCK_FAILURE_NONE;
};



} // namespace cluck_deamon
// vim: ts=4 sw=4 et
