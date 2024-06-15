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
#include    "messenger.h"

#include    "cluckd.h"


// cluck
//
#include    <cluck/names.h>


// eventdispatcher
//
#include    <eventdispatcher/names.h>


// communicatord
//
#include    <communicatord/names.h>


// last include
//
#include    <snapdev/poison.h>



namespace cluck_daemon
{


/** \class messenger
 * \brief Handle messages from the communicatord.
 *
 * This class is an implementation of the TCP client message connection
 * so we can handle incoming messages. We actually use the fuild-settings
 * which itself uses the communicatord connection. All of the basic
 * communication messages used by the communicatord and fluid settings
 * are handled automatically.
 *
 * This class handles the lock messages.
 */



/** \brief The messenger initialization.
 *
 * The messenger is the cluck daemon connection to the communicator server.
 *
 * It sets up its dispatcher and calls cluckd functions whenever it
 * receives a message.
 *
 * \param[in] c  The cluck object we are listening for.
 * \param[in] opts  The options received from the command line.
 */
messenger::messenger(cluckd * c, advgetopt::getopt & opts)
    : fluid_settings_connection(opts, "cluckd")
    , f_cluckd(c)
    , f_dispatcher(std::make_shared<ed::dispatcher>(this))
{
    set_name("messenger");
    set_dispatcher(f_dispatcher);
    add_fluid_settings_commands();
    f_dispatcher->add_matches({
        // eventdispatcher commands
        //
        ed::define_match(
              ed::Expression(ed::g_name_ed_cmd_absolutely)
            , ed::Callback(std::bind(&cluckd::msg_absolutely, c, std::placeholders::_1))
        ),

        // communicatord commands
        //
        ed::define_match(
              ed::Expression(communicatord::g_name_communicatord_cmd_cluster_down)
            , ed::Callback(std::bind(&cluckd::msg_cluster_down, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(communicatord::g_name_communicatord_cmd_cluster_up)
            , ed::Callback(std::bind(&cluckd::msg_cluster_up, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(communicatord::g_name_communicatord_cmd_disconnected)
            , ed::Callback(std::bind(&cluckd::msg_server_gone, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(communicatord::g_name_communicatord_cmd_hangup)
            , ed::Callback(std::bind(&cluckd::msg_server_gone, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(communicatord::g_name_communicatord_cmd_status)
            , ed::Callback(std::bind(&cluckd::msg_status, c, std::placeholders::_1))
            , ed::MatchFunc(&ed::one_to_one_callback_match)
            , ed::Priority(ed::dispatcher_match::DISPATCHER_MATCH_CALLBACK_PRIORITY)
        ),

        // cluck commands
        //
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_activate_lock)
            , ed::Callback(std::bind(&cluckd::msg_activate_lock, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_add_ticket)
            , ed::Callback(std::bind(&cluckd::msg_add_ticket, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_drop_ticket)
            , ed::Callback(std::bind(&cluckd::msg_drop_ticket, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_get_max_ticket)
            , ed::Callback(std::bind(&cluckd::msg_get_max_ticket, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_info)
            , ed::Callback(std::bind(&cluckd::msg_info, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_list_tickets)
            , ed::Callback(std::bind(&cluckd::msg_list_tickets, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock)
            , ed::Callback(std::bind(&cluckd::msg_lock, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock_activated)
            , ed::Callback(std::bind(&cluckd::msg_lock_activated, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock_failed)
            , ed::Callback(std::bind(&cluckd::msg_lock_failed, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock_leaders)
            , ed::Callback(std::bind(&cluckd::msg_lock_leaders, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock_started)
            , ed::Callback(std::bind(&cluckd::msg_lock_started, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock_status)
            , ed::Callback(std::bind(&cluckd::msg_lock_status, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock_tickets)
            , ed::Callback(std::bind(&cluckd::msg_lock_tickets, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_max_ticket)
            , ed::Callback(std::bind(&cluckd::msg_max_ticket, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_ticket_added)
            , ed::Callback(std::bind(&cluckd::msg_ticket_added, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_ticket_ready)
            , ed::Callback(std::bind(&cluckd::msg_ticket_ready, c, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_unlock)
            , ed::Callback(std::bind(&cluckd::msg_unlock, c, std::placeholders::_1))
        ),
    });
    f_dispatcher->add_communicator_commands();

    // further dispatcher initialization
    //
#ifdef _DEBUG
    f_dispatcher->set_trace();
    f_dispatcher->set_show_matches();
#endif
}


messenger::~messenger()
{
}


/** \brief Finish handling command line options.
 *
 * This function makes sure the fluid settings and communicator daemon
 * have a chance to check the command line options and act on it.
 */
void messenger::finish_parsing()
{
    process_fluid_settings_options();
    automatic_watch_initialization();
}


/** \brief Send the CLUSTER_STATUS to communicatord once ready.
 *
 * This function builds a message and sends it to communicatord.
 *
 * The CLUSTER_UP and CLUSTER_DOWN messages are sent only when that specific
 * event happen and until then we do not know what the state really is
 * (although we assume the worst and use CLUSTER_DOWN until we get a reply).
 *
 * \param[in] status  The status of the fluid settings object.
 * \param[in] name  The name of the changing parameter.
 * \param[in] value  The value of the parameter.
 */
void messenger::fluid_settings_changed(
      fluid_settings::fluid_settings_status_t status
    , std::string const & name
    , std::string const & value)
{
    snapdev::NOT_USED(name, value);

    if(status == fluid_settings::fluid_settings_status_t::FLUID_SETTINGS_STATUS_READY)
    {
        // now we're ready to start with cluckd
        //
        ed::message clusterstatus_message;
        clusterstatus_message.set_command(cluck::g_name_cluck_cmd_cluster_status);
        clusterstatus_message.set_service(communicatord::g_name_communicatord_service_communicatord);
        send_message(clusterstatus_message);
    }
}



} // namespace cluck_daemon
// vim: ts=4 sw=4 et
