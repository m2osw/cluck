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
#include    "server.h"


// cluck
//
#include    <cluck/names.h>
#include    <cluck/version.h>


// communicatord
//
#include    <communicatord/names.h>


// snapdev
//
#include    <snapdev/stringize.h>


// advgetopt
//
#include    <advgetopt/exception.h>


// snaplogger
//
#include    <snaplogger/message.h>
#include    <snaplogger/options.h>


// last include
//
#include    <snapdev/poison.h>



namespace lock_status
{



namespace
{



advgetopt::option const g_options[] =
{
    advgetopt::define_option(
          advgetopt::Name("list-ticket")
        , advgetopt::ShortName('l')
        , advgetopt::Flags(advgetopt::standalone_command_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("List existing tickets.")
    ),
    advgetopt::define_option(
          advgetopt::Name("quiet")
        , advgetopt::ShortName('q')
        , advgetopt::Flags(advgetopt::standalone_command_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("Make command as quiet as possible.")
    ),
    advgetopt::define_option(
          advgetopt::Name("server-name")
        , advgetopt::ShortName('n')
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_DYNAMIC_CONFIGURATION
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("Set the name of this server instance.")
        , advgetopt::DefaultValue("cluckd")
    ),
    advgetopt::end_options()
};


advgetopt::group_description const g_group_descriptions[] =
{
    advgetopt::define_group(
          advgetopt::GroupNumber(advgetopt::GETOPT_FLAG_GROUP_COMMANDS)
        , advgetopt::GroupName("command")
        , advgetopt::GroupDescription("Commands:")
    ),
    advgetopt::define_group(
          advgetopt::GroupNumber(advgetopt::GETOPT_FLAG_GROUP_OPTIONS)
        , advgetopt::GroupName("option")
        , advgetopt::GroupDescription("Options:")
    ),
    advgetopt::end_groups()
};


advgetopt::options_environment const g_options_environment =
{
    .f_project_name = "lock-status",
    .f_group_name = "cluck",
    .f_options = g_options,
    .f_environment_variable_name = "LOCK_STATUS_OPTIONS",
    .f_environment_flags = advgetopt::GETOPT_ENVIRONMENT_FLAG_SYSTEM_PARAMETERS
                         | advgetopt::GETOPT_ENVIRONMENT_FLAG_PROCESS_SYSTEM_PARAMETERS,
    .f_help_header = "Usage: %p [-<opt>]\n"
                     "where -<opt> is one or more of:",
    .f_help_footer = "%c",
    .f_version = CLUCK_VERSION_STRING,
    .f_license = "GNU GPL v3",
    .f_copyright = "Copyright (c) 2013-"
                   SNAPDEV_STRINGIZE(UTC_BUILD_YEAR)
                   " by Made to Order Software Corporation -- All Rights Reserved",
    .f_groups = g_group_descriptions,
};



}
// no name namespace



/** \class server
 * \brief Handle command line and start the messenger.
 *
 * This class is a server. It creates a messenger to send a message to
 * the cluckd service and print out the response.
 */



/** \brief The server initialization.
 *
 * The server creates a connection to communicatord. Once ready, it
 * sends a LIST_TICKETS and receives the answer in a TICKET_LIST.
 *
 * \param[in] argc  The number of arguments in argv.
 * \param[in] argv  The array of command line arguments.
 */
server::server(int argc, char * argv[])
    : f_opts(g_options_environment)
{
    snaplogger::add_logger_options(f_opts);
    f_opts.finish_parsing(argc, argv);
    if(!snaplogger::process_logger_options(f_opts, "/etc/cluck/logger"))
    {
        throw advgetopt::getopt_exit("logger options generated an error.", 0);
    }
}


int server::run()
{
    f_communicator = ed::communicator::instance();
    f_messenger = std::make_shared<messenger>(this, f_opts);
    f_communicator->add_connection(f_messenger);
    return f_communicator->run() ? 0 : 1;
}


/** \brief Print out the returned list of tickets.
 *
 * This function gets called whenever we receive the list of tickets.
 * It prints out the list and exits.
 *
 * \param[in] msg  The TICKET_LIST message.
 */
//void server::msg_ticket_list(ed::message & msg)
//{
//    std::string const list(msg.get_parameter("list"));
//
//    // add newlines for people who have TRACE mode would otherwise have
//    // a hard time to find the actual list
//    //
//    if(list.empty())
//    {
//        if(!f_quiet)
//        {
//            std::cout << std::endl << "...no locks found..." << std::endl;
//        }
//    }
//    else
//    {
//        std::cout << std::endl << list << std::endl;
//    }
//
//    stop(false);
//}


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
//void server::msg_transmission_report(ed::message & msg)
//{
//    std::string const status(msg.get_parameter("status"));
//    if(status == "failed")
//    {
//        SNAP_LOG_ERROR
//            << "the transmission of our "
//            << cluck::g_name_cluck_cmd_list_tickets
//            << " message failed to travel to a cluckd service"
//            << SNAP_LOG_SEND;
//        stop(false);
//    }
//}



} // namespace lock_status
// vim: ts=4 sw=4 et
