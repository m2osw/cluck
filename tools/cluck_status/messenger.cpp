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


// self
//
#include    "messenger.h"

#include    "server.h"


// cluck
//
#include    <cluck/names.h>


// advgetopt
//
#include    <advgetopt/exception.h>


// communicator
//
#include    <communicator/names.h>


// snaplogger
//
#include    <snaplogger/message.h>


// last include
//
#include    <snapdev/poison.h>



namespace lock_status
{



/** \class messenger
 * \brief Tool used to send messages to a running cluckd service.
 *
 * This class is a messenger used to send a message to the cluckd service
 * and print out the response.
 *
 * At the moment, we have a single such command: LIST_TICKETS.
 */



/** \brief The messenger initialization.
 *
 * The messenger is a connection to the communicatord server. It is
 * used to send a LIST_TICKETS and receive the answer in a TICKET_LIST.
 *
 * \param[in] s  The server we are listening for.
 * \param[in] opts  The command line and other options.
 */
messenger::messenger(server * s, advgetopt::getopt & opts)
    : fluid_settings_connection(opts, "cluckd")
    , f_server(s)
    , f_dispatcher(std::make_shared<ed::dispatcher>(this))
    , f_quiet(opts.is_defined("quiet"))
    , f_server_name(opts.get_string("server-name"))
{
    set_name("lock_status");
    set_dispatcher(f_dispatcher);
    f_dispatcher->add_communicator_commands();
    f_dispatcher->add_matches({
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_lock_ready)
            , ed::Callback(std::bind(&messenger::msg_lock_ready, this, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_no_lock)
            , ed::Callback(std::bind(&messenger::msg_no_lock, this, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(cluck::g_name_cluck_cmd_ticket_list)
            , ed::Callback(std::bind(&messenger::msg_ticket_list, this, std::placeholders::_1))
        ),
        ed::define_match(
              ed::Expression(::communicator::g_name_communicator_cmd_transmission_report)
            , ed::Callback(std::bind(&messenger::msg_transmission_report, this, std::placeholders::_1))
        ),
    });

    if(opts.is_defined("list-ticket"))
    {
        f_command = cluck::g_name_cluck_cmd_list_tickets;
    }
    else if(opts.is_defined("lock-status"))
    {
        f_command = cluck::g_name_cluck_cmd_lock_status;
    }
    else
    {
        throw advgetopt::getopt_exit("no command was specified.", 0);
    }

    // further dispatcher initialization
    //
#ifdef _DEBUG
    f_dispatcher->set_trace();
    f_dispatcher->set_show_matches();
#endif
}


/** \brief Capture the changes of settings.
 *
 * This function is used to know once the fluid-settings client is ready.
 * At that point, we can send the LIST_TICKETS message to a cluckd service.
 *
 * \param[in] status  The status of the fluid setting that changed.
 * \param[in] name  The name of the setting that changed. May be empty if
 * the status says so.
 * \param[in] value  The value of the setting.
 */
void messenger::fluid_settings_changed(
      fluid_settings::fluid_settings_status_t status
    , std::string const & name
    , std::string const & value)
{
    fluid_settings_connection::fluid_settings_changed(status, name, value);

    if(status == fluid_settings::fluid_settings_status_t::FLUID_SETTINGS_STATUS_READY)
    {
        // send the LIST_TICKETS to get the list of currently active tickets
        //
        ed::message list_message;
        list_message.set_command(f_command);
        list_message.set_service(cluck::g_name_cluck_service_name);
        list_message.set_server(f_server_name);
        list_message.add_parameter("cache", "no");
        ::communicator::request_failure(list_message);
        send_message(list_message);
    }
}


void messenger::msg_lock_ready(ed::message & msg)
{
    snapdev::NOT_USED(msg);

    std::cout << "ready\n";
}


void messenger::msg_no_lock(ed::message & msg)
{
    snapdev::NOT_USED(msg);

    std::cout << "no-lock\n";
}


/** \brief Print out the returned list of tickets.
 *
 * This function gets called whenever we receive the list of tickets.
 * It prints out the list and exits.
 *
 * \param[in] msg  The TICKET_LIST message.
 */
void messenger::msg_ticket_list(ed::message & msg)
{
    std::string const list(msg.get_parameter("list"));

    // add newlines for people who have TRACE mode would otherwise have
    // a hard time to find the actual list
    //
    if(list.empty())
    {
        if(!f_quiet)
        {
            std::cout << std::endl << "...no locks found..." << std::endl;
        }
    }
    else
    {
        std::cout << std::endl << list << std::endl;
    }

    stop(false);
}


/** \brief Got a transmission report.
 *
 * Whenever the communicatord service sends a message on our behalf, it
 * can sends us a report about whether the message was sent successfully
 * or not. We use this message to report a failure to the user which
 * happens when the communicatord service cannot forward our messages to
 * a cluckd service.
 *
 * \param[in] msg  The transmission report message.
 */
void messenger::msg_transmission_report(ed::message & msg)
{
    std::string const status(msg.get_parameter(::communicator::g_name_communicator_param_status));
    if(status == ::communicator::g_name_communicator_value_failed)
    {
        SNAP_LOG_ERROR
            << "the transmission of our \""
            << f_command
            << "\" message failed to travel to a cluckd service."
            << SNAP_LOG_SEND;

        stop(false);
    }
}



} // namespace lock_status
// vim: ts=4 sw=4 et
