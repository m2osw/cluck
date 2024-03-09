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
        ed::define_match(
              ed::Expression(communicatord::g_name_communicatord_cmd_cluster_down)
            , ed::Callback(std::bind(&messenger::msg_cluster_down, this, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(communicatord::g_name_communicatord_cmd_cluster_up)
            , ed::Callback(std::bind(&messenger::msg_cluster_up, this, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_list_tickets)
            , ed::Callback(std::bind(&messenger::msg_list_tickets, this, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock)
            , ed::Callback(std::bind(&messenger::msg_lock, this, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock_status)
            , ed::Callback(std::bind(&messenger::msg_lock_status, this, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_unlock)
            , ed::Callback(std::bind(&messenger::msg_unlock, this, std::placeholders::_1))
        ),
    });
    f_dispatcher->add_communicator_commands();

    // further dispatcher initialization
    //
#ifdef _DEBUG
    f_dispatcher->set_trace();
    f_dispatcher->set_show_matches();
#endif

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


/** \brief The communicatord lost too many connections.
 *
 * When the cluster is not complete, the CLUSTER_DOWN message gets sent
 * by the communicatord. We need to stop offering locks at that point.
 * Locks that are up are fine, but new locks are not possible.
 *
 * \param[in] msg  The CLUSTER_DOWN message.
 */
void messenger::msg_cluster_down(ed::message & msg)
{
    snapdev::NOT_USED(msg);

    f_cluckd->cluster_down();
}


/** \brief Cluster is ready, send the LOCK_STARTED message.
 *
 * Our cluster is finally ready, so we can send the LOCK_STARTED and work
 * on a leader election if still required.
 */
void messenger::msg_cluster_up(ed::message & msg)
{
    f_cluckd->set_neighbors_count(msg.get_integer_parameter("neighbors_count"));
    f_cluckd->election_status();
    f_cluckd->send_lock_started(nullptr);
}


/** \brief Reply to the LIST_TICKETS message with the TICKET_LIST.
 *
 * This function gets called whenever the command line tool (`cluckd`)
 * is run with the `--list` command line option. It generates a list of
 * tickets and sends that back to the tool as a TICKET_LIST message.
 *
 * \param[in] msg  The LIST_TICKETS message.
 */
void messenger::msg_list_tickets(ed::message & msg)
{
    ed::message list_message;
    list_message.set_command(cluck::g_name_cluck_cmd_ticket_list);
    list_message.reply_to(msg);
    list_message.add_parameter("list", f_cluckd->ticket_list());
    send_message(list_message);
}


/** \brief The messenger received a LOCK message.
 *
 * This function is called whenever the messengers receives a LOCK message.
 * The function calls the cluck::msg_lock() function.
 *
 * \param[in] msg  The LOCK message.
 */
void messenger::msg_lock(ed::message & msg)
{
    f_cluckd->msg_lock(msg);
}


/** \brief A service asked about the lock status.
 *
 * The lock status is whether the cluck service is ready to receive
 * LOCK messages (LOCK_READY) or is still waiting on a CLUSTER_UP and
 * LOCK_LEADERS to happen (NO_LOCK).
 *
 * Note that LOCK messages are accepted while the lock service is not
 * yet ready, however, those are cached and it is more likely that they
 * timeout before the system is ready to process the request.
 *
 * \param[in] msg  The message to reply to.
 */
void messenger::msg_lock_status(ed::message & msg)
{
    ed::message status_message;
    status_message.set_command(f_cluckd->is_ready()
            ? cluck::g_name_cluck_cmd_lock_ready
            : cluck::g_name_cluck_cmd_no_lock);
    status_message.reply_to(msg);
    status_message.add_parameter("cache", "no");
    send_message(status_message);
}


/** \brief The messenger received an UNLOCK message.
 *
 * This function is called whenever the messenger receives an UNLOCK message.
 * The function calls the cluck::msg_unlock() function.
 *
 * \param[in] msg  The LOCK message.
 */
void messenger::msg_unlock(ed::message & msg)
{
    f_cluckd->msg_unlock(msg);
}



} // namespace cluck_daemon
// vim: ts=4 sw=4 et
