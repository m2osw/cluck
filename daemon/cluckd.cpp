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
#include    "cluckd.h"



// cluck
//
#include    <cluck/exception.h>
#include    <cluck/names.h>
#include    <cluck/version.h>


// communicatord
//
#include    <communicatord/names.h>


// eventdispatcher
//
#include    <eventdispatcher/names.h>


// snapdev
//
#include    <snapdev/gethostname.h>
#include    <snapdev/hexadecimal_string.h>
#include    <snapdev/stringize.h>
#include    <snapdev/tokenize_string.h>
#include    <snapdev/to_string_literal.h>


// snaplogger
//
#include    <snaplogger/logger.h>
#include    <snaplogger/options.h>
#include    <snaplogger/severity.h>


// advgetopt
//
#include    <advgetopt/advgetopt.h>
#include    <advgetopt/exception.h>


// C++
//
#include    <algorithm>
#include    <iostream>
#include    <sstream>


// openssl
//
#include    <openssl/rand.h>


// last include
//
#include    <snapdev/poison.h>



/** \file
 * \brief Implementation of the inter-process lock mechanism.
 *
 * This file implements an inter-process lock that functions between
 * any number of machines. The basic algorithm used is the Bakery
 * Algorithm by Lamport. The concept is simple: you get a waiting
 * ticket and loop through the tickets until it yours is picked.
 *
 * Contrary to a multi-processor environment thread synchronization,
 * this lock system uses messages and arrays to know its current
 * state. A user interested in obtaining a lock sends a LOCK
 * message. The cluck daemon then waits until the lock is
 * obtained and sends a LOCKED as a reply. Once done with the lock,
 * the user sends UNLOCK to release the lock.
 *
 * The implementation makes use of any number of cluck instances.
 * The locking mechanism makes use of the QUORUM voting system to
 * know that enough of the other cluck agree on a statement.
 * This allows the cluck daemon to obtain/release locks in an
 * unknown network environment (i.e. any one of the machines may
 * be up or down and the locking mechanism still functions as
 * expected).
 *
 * The QUORUM is computed using the following formula:
 *
 * \f[
 *      {number of computers} over 2 + 1
 * \f]
 *
 * Note that the lock mechanism itself only uses 3 computers (2 or 1 if
 * your cluster is that small). So in a cluster of, say, 100 computers,
 * you would still end up using just 3 for the locks. However, the
 * elections still require you to have a quorum of all the computers
 * (100 / 2 + 1 is at least 51 computers).
 *
 * \note
 * The cluck implementation checks parameters and throws away
 * messages that are definitely not going to be understood. However,
 * it is, like most Snap! daemons, very trustworthy of other cluck
 * daemons and does not expect other daemons to mess around with its
 * sequence of lock messages used to ensure that everything worked as
 * expected.
 */



namespace cluck_daemon
{


namespace
{



constexpr std::string_view  g_default_candidate_priority =
    snapdev::integer_to_string_literal<computer::PRIORITY_DEFAULT>.data();


advgetopt::option const g_options[] =
{
    advgetopt::define_option(
          advgetopt::Name("candidate-priority")
        , advgetopt::ShortName('p')
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("Define the priority of this candidate (1 to 14) to gain a leader position or \"off\".")
        , advgetopt::DefaultValue(g_default_candidate_priority.data())
    ),
    advgetopt::define_option(
          advgetopt::Name("server-name")
        , advgetopt::ShortName('n')
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_DYNAMIC_CONFIGURATION
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("Set the name of this server instance.")
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


constexpr char const * const g_configuration_files[] =
{
    "/etc/cluck/cluckd.conf",
    nullptr
};


advgetopt::options_environment const g_options_environment =
{
    .f_project_name = "cluckd",
    .f_group_name = "cluck",
    .f_options = g_options,
    .f_environment_variable_name = "CLUCKD_OPTIONS",
    .f_configuration_files = g_configuration_files,
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













/** \class cluckd
 * \brief Class handling intercomputer locking.
 *
 * This class is used in order to create intercomputer locks on request.
 *
 * The class uses Snap! Communicator messages and implements
 * the LOCK and UNLOCK commands and sends the LOCKED and UNLOCKED
 * commands to its senders.
 *
 * The system makes use of the Lamport's Bakery Algorithm. This is
 * explained in the ticket class.
 *
 * \note
 * At this time, there is one potential problem that can arise: the
 * lock may fail to concretize because the computer to which you
 * first sent the LOCK message goes down in some way. The other
 * cluck computers will have no clue by which computer the lock
 * was being worked on and whether one of them should take over.
 * One way to remediate is to run one instance of cluck on each
 * computer on which a lock is likely to happen. Then the LOCK
 * message will be proxied to the proper destination (a leader).
 *
 * \warning
 * The LOCK mechanism uses the system clock of each computer to know when
 * a lock times out. You are responsible for making sure that all those
 * computers have a synchronized clocked (i.e. run a timed daemon such
 * as ntpd). The difference in time should be as small as possible. The
 * precision required by cluck is around 1 second.
 *
 * The following shows the messages used to promote 3 leaders, in other
 * words it shows how the election process happens. The election itself
 * is done on the computer that is part of the cluster considered to be up
 * and which has the smallest IP address. That's the one computer that will
 * send the LOCKLEADERS. As soon as that happens all the other nodes on the
 * cluster know the leaders and inform new nodes through the LOCKSTARTED
 * message.
 *
 * \msc
 *  Communicator,A,B,C,D,E,F;
 *
 *  A->Communicator [label="REGISTER"];
 *  Communicator->A [label="HELP"];
 *  Communicator->A [label="READY"];
 *
 *  A->Communicator [label="CLUSTERSTATUS"];
 *  Communicator->A [label="CLUSTERUP"];
 *
 *  # Broadcast to B to F, but we do not know who's up at this point
 *  A->* [label="LOCKSTARTED"];
 *
 *  # A answers each one of those because for it B, C, D, ... are new
 *  B->A [label="LOCKSTARTED"];
 *  A->B [label="LOCKSTARTED"];
 *
 *  C->A [label="LOCKSTARTED"];
 *  A->C [label="LOCKSTARTED"];
 *
 *  D->A [label="LOCKSTARTED"];
 *  A->D [label="LOCKSTARTED"];
 *
 *  # When we reach here we have a CLUSTERUP in terms of cluck daemons
 *  # Again here we broadcast, maybe we should send to known computers instead?
 *  # IMPORTANT: A determines the leaders only if its IP is the smallest
 *  A->* [label="LOCKLEADERS"];
 *
 *  # Here the replies from A will include the leaders
 *  # Of course, as shown above, E will have sent the message to all and
 *  # so it will receive the leaders from multiple sources
 *  E->A [label="LOCKSTARTED"];
 *  A->E [label="LOCKSTARTED"];
 *
 *  F->A [label="LOCKSTARTED"];
 *  A->F [label="LOCKSTARTED"];
 * \endmsc
 *
 * \sa ticket
 */



/** \brief Initializes a cluckd object.
 *
 * This function parses the command line arguments, reads configuration
 * files, setups the logger.
 *
 * It also immediately executes a --help or a --version command line
 * option and exits the process if these are present.
 *
 * \param[in] argc  The number of arguments in the argv array.
 * \param[in] argv  The array of argument strings.
 */
cluckd::cluckd(int argc, char * argv[])
    : f_opts(g_options_environment)
{
    snaplogger::add_logger_options(f_opts);

    // before we can parse command line arguments, we must create the
    // fluid settings & communicator client objects which happen to
    // dynamically add command line options to f_opts
    //
    f_messenger = std::make_shared<messenger>(this, f_opts);

    f_opts.finish_parsing(argc, argv);
    if(!snaplogger::process_logger_options(f_opts, "/etc/cluck/logger"))
    {
        throw advgetopt::getopt_exit("logger options generated an error.", 0);
    }

    // get the server name using the library function
    //
    // TODO: if the name of the server is changed, we should reboot, but
    //       to the minimum we need to restart cluck (among other daemons)
    //       remember that snapmanager.cgi gives you that option;
    //       we CANNOT use fluid-settings for this one since each computer
    //       must have a different name
    //
    if(f_opts.is_defined("server-name"))
    {
        f_server_name = f_opts.get_string("server-name");
    }
    if(f_server_name.empty())
    {
        f_server_name = snapdev::gethostname();
    }

    f_start_time = snapdev::now();
}


/** \brief Do some clean ups.
 *
 * At this point, the destructor is present mainly because we have
 * some virtual functions.
 */
cluckd::~cluckd()
{
}


/** \brief Finish the cluck daemon initialization.
 *
 * This function creates all the connections used by the cluck daemon.
 *
 * \note
 * This is separate from the run() function so we can run unit tests
 * against the cluck daemon.
 *
 * \sa run()
 */
void cluckd::add_connections()
{
    f_communicator = ed::communicator::instance();

    // capture Ctrl-C (SIGINT) to get a clean exit by default
    //
    f_interrupt = std::make_shared<interrupt>(this);
    f_communicator->add_connection(f_interrupt);

    // timer so we can timeout locks
    //
    f_timer = std::make_shared<timer>(this);
    f_communicator->add_connection(f_timer);

    // create a messenger to connect with the Communicator daemon
    // and other services as required
    //
    f_communicator->add_connection(f_messenger);

    // the following call connects to the communicator daemon
    //
    f_messenger->finish_parsing();
}


/** \brief Run the cluck daemon.
 *
 * This function is the core function of the daemon. It runs the loop
 * used to lock processes from any number of computers that have access
 * to the cluck daemon network.
 *
 * \sa add_connections()
 */
void cluckd::run()
{
    SNAP_LOG_INFO
        << "--------------------------------- cluckd started."
        << SNAP_LOG_SEND;

    // now run our listening loop
    //
    f_communicator->run();
}





/** \brief Return the number of known computers running cluckd.
 *
 * This function is used by the ticket objects to calculate
 * the quorum so as to know how many computers need to reply to
 * our messages before we can be sure we got the correct
 * results.
 *
 * \return The number of instances of cluckd running and connected.
 */
int cluckd::get_computer_count() const
{
    return f_computers.size();
}


/** \brief Get the name of the server we are running on.
 *
 * This function returns the name of the server this instance of
 * cluck is running. It is used by the ticket implementation
 * to know whether to send a reply to the cluck object (i.e.
 * at this time we can send messages to that object only from the
 * server it was sent from).
 *
 * \return The name of the server cluck is running on.
 */
std::string const & cluckd::get_server_name() const
{
    return f_server_name;
}


/** \brief Check whether the cluck daemon is ready to process lock requests.
 *
 * This function checks whether the cluck daemon is ready by looking
 * at whether it has leaders and if so, whether each leader is connected.
 *
 * Once both tests succeeds, this cluck daemon can forward the locks to
 * the leaders. If it is a leader itself, it can enter a ticket in
 * the selection and message both of the other leaders about it.
 *
 * \return true once locks can be processed.
 */
bool cluckd::is_daemon_ready() const
{
    // we are not yet properly registered
    //
    if(f_messenger == nullptr || !f_messenger->is_ready())
    {
        return false;
    }

    // without at least one leader we are definitely not ready
    //
    if(f_leaders.empty())
    {
        SNAP_LOG_TRACE
            << "not considered ready: no leaders."
            << SNAP_LOG_SEND;
        return false;
    }

    // enough leaders for that cluster?
    //
    // we consider that having at least 2 leaders is valid because locks
    // will still work, an election should be happening when we lose a
    // leader fixing that temporary state
    //
    // the test below allows for the case where we have a single computer
    // too (i.e. "one neighbor")
    //
    // notice how not having received the CLUSTERUP would be taken in
    // account here since f_neighbors_count will still be 0 in that case
    // (however, the previous empty() test already takes that in account)
    //
    if(f_leaders.size() == 1
    && f_neighbors_count != 1)
    {
        SNAP_LOG_TRACE
            << "not considered ready: not enough leaders for this cluster."
            << SNAP_LOG_SEND;
        return false;
    }

    // the election_status() function verifies that the quorum is
    // attained, but it can change if the cluster grows or shrinks
    // so we have to check here again as the lock system becomes
    // "unready" when the quorum is lost; see that other function
    // for additional info

    // this one probably looks complicated...
    //
    // if our quorum is 1 or 2 then we need a number of computers
    // equal to the total number of computers (i.e. a CLUSTER_COMPLETE
    // status which we verify here)
    //
    if(f_neighbors_quorum < 3
    && f_computers.size() < f_neighbors_count)
    {
        SNAP_LOG_TRACE
            << "not considered ready: quorum changed, re-election expected soon."
            << SNAP_LOG_SEND;
        return false;
    }

    // the neighbors count & quorum can change over time so
    // we have to verify that the number of computers is
    // still acceptable here
    //
    if(f_computers.size() < f_neighbors_quorum)
    {
        SNAP_LOG_TRACE
            << "not considered ready: quorum lost, re-election expected soon."
            << SNAP_LOG_SEND;
        return false;
    }

    // are all leaders connected to us?
    //
    for(auto const & l : f_leaders)
    {
        if(!l->get_connected())
        {
            SNAP_LOG_TRACE
                << "not considered ready: no direct connection with leader: \""
                << l->get_name()
                << "\"."
                << SNAP_LOG_SEND;

            // attempt resending a LOCKSTARTED because it could be that it
            // did not work quite right and the cluck daemons are not
            // going to ever talk with each others otherwise
            //
            // we also make sure we do not send the message too many times,
            // in five seconds it should be resolved...
            //
            time_t const now(time(nullptr));
            if(now > f_pace_lockstarted)
            {
                // pause for 5 to 6 seconds in case this happens a lot
                //
                f_pace_lockstarted = now + 5;

                // only send it to that specific server cluck daemon
                //
                ed::message temporary_message;
                temporary_message.set_sent_from_server(l->get_name());
                temporary_message.set_sent_from_service(cluck::g_name_cluck_service_name);
                const_cast<cluckd *>(this)->send_lock_started(&temporary_message);
            }

            return false;
        }
    }

    // it looks like we are ready
    //
    return true;
}


/** \brief Search for a leader.
 *
 * This function goes through the list of leaders to determine whether
 * the specified identifier represents a leader. If so it returns a pointer
 * to that leader computer object.
 *
 * When the function is called with an empty string as the computer
 * identifier, this computer is checked to see whether it is a leader.
 *
 * \warning
 * This function is considered slow since it goes through the list of leaders
 * on each call. On the other hand, it's only 1 to 3 leaders. Yet, you should
 * cache the result within your function if you need the result multiple
 * times, as in:
 *
 * \code
 *      computer::pointer_t leader(is_leader());
 *      // ... then use `leader` any number of times ...
 * \endcode
 *
 * \par
 * This is done that way because the function may return a different result
 * over time (i.e. if a re-election happens).
 *
 * \param[in] id  The identifier of the leader to search, if empty, default
 *                to f_my_id (i.e. whether this cluckd is a leader).
 *
 * \return The leader computer::pointer_t or a null pointer.
 */
computer::pointer_t cluckd::is_leader(std::string id) const
{
    if(id.empty())
    {
        id = f_my_id;
    }

    auto const l(std::find_if(
          f_leaders.begin()
        , f_leaders.end()
        , [id](auto const & c){
              return c->get_id() == id;
          }));
    if(l != f_leaders.end())
    {
        return *l;
    }

    return computer::pointer_t();
}


computer::pointer_t cluckd::get_leader_a() const
{
#ifdef _DEBUG
    if(is_leader() == nullptr)
    {
        throw cluck::logic_error("cluckd::get_leader_a(): only a leader can call this function.");
    }
#endif

    switch(f_leaders.size())
    {
    case 0: // LCOV_EXCL -- because of the debug above, this cannot happen here
    default:
        throw cluck::logic_error("cluckd::get_leader_a(): call this function only when leaders were elected."); // LCOV_EXCL

    case 1:
        return computer::pointer_t();

    case 2:
    case 3:
        return f_leaders[f_leaders[0]->is_self() ? 1 : 0];

    }
}


computer::pointer_t cluckd::get_leader_b() const
{
#ifdef _DEBUG
    if(is_leader() == nullptr)
    {
        throw cluck::logic_error("cluckd::get_leader_b(): only a leader can call this function.");
    }
#endif

    switch(f_leaders.size())
    {
    case 0: // LCOV_EXCL -- because of the debug above, this cannot happen here
    default:
        throw cluck::unexpected_case("cluckd::get_leader_b(): call this function only when leaders were elected."); // LCOV_EXCL

    case 1:
    case 2: // we have a leader A but no leader B when we have only 2 leaders
        return computer::pointer_t();

    case 3:
        return f_leaders[f_leaders[2]->is_self() ? 1 : 2];

    }
}



/** \brief Output the state of this cluckd object.
 *
 * This function outputs the current state of a cluck daemon to
 * the cluckd.log file.
 *
 * This is used to debug a cluck instance and make sure that the
 * state is how you would otherwise expect it to be.
 *
 * \param[in] msg  The INFO message.
 */
void cluckd::msg_info(ed::message & msg)
{
    SNAP_LOG_INFO
        << "++++++++ CLUCK INFO ++++++++"
        << SNAP_LOG_SEND;
    SNAP_LOG_INFO
        << "My leader ID: "
        << (f_my_id.empty() ? "<not ready>" : f_my_id)
        << SNAP_LOG_SEND;
    addr::addr zero;
    SNAP_LOG_INFO
        << "My IP address: "
        << (f_my_ip_address == zero ? "<not assigned>" : f_my_ip_address.to_ipv4or6_string(addr::STRING_IP_ADDRESS | addr::STRING_IP_BRACKET_ADDRESS))
        << SNAP_LOG_SEND;
    SNAP_LOG_INFO
        << "Total number of computers: "
        << f_neighbors_count
        << " (quorum: "
        << f_neighbors_quorum
        << ", leaders: "
        << f_leaders.size()
        << ")"
        << SNAP_LOG_SEND;
    SNAP_LOG_INFO
        << "Known computers: "
        << f_computers.size()
        << SNAP_LOG_SEND;
    for(auto const & c : f_computers)
    {
        auto const it(std::find_if(
                  f_leaders.begin()
                , f_leaders.end()
                , [&c](auto const & l)
                {
                    return c.second == l;
                }));
        std::string leader;
        if(it != f_leaders.end())
        {
            leader = " (LEADER #";
            leader += std::to_string(it - f_leaders.begin());
            leader += ')';
        }
        SNAP_LOG_INFO
            << " --          Computer Name: "
            << c.second->get_name()
            << leader
            << SNAP_LOG_SEND;
        SNAP_LOG_INFO
            << " --            Computer ID: "
            << c.second->get_id()
            << SNAP_LOG_SEND;
        SNAP_LOG_INFO
            << " --    Computer IP Address: "
            << c.second->get_ip_address()
            << SNAP_LOG_SEND;
    }

    if(msg.has_parameter(cluck::g_name_cluck_param_mode)
    && msg.get_parameter(cluck::g_name_cluck_param_mode) == cluck::g_name_cluck_value_debug)
    {
        SNAP_LOG_INFO
            << "++++ serialized tickets: "
            << serialized_tickets()
            << SNAP_LOG_SEND;
    }
}



/** \brief Generate the output for "cluck-status --list"
 *
 * This function loops over the list of tickets and outputs a string that
 * it sends back to the `cluck-status --list` tool for printing to the administrator.
 *
 * \return A string with the list of all the tickets.
 */
std::string cluckd::ticket_list() const
{
    std::stringstream list;
    for(auto const & obj_ticket : f_tickets)
    {
        for(auto const & key_ticket : obj_ticket.second)
        {
            list
                << "ticket_id: "
                << key_ticket.second->get_ticket_number()
                << "  object name: \""
                << key_ticket.second->get_object_name()
                << "\"  key: "
                << key_ticket.second->get_entering_key()
                << "  ";

            cluck::timeout_t const lock_timeout(key_ticket.second->get_lock_timeout_date());
            if(!lock_timeout)
            {
                list
                    << "timeout "
                    << lock_timeout.to_string();
            }
            else
            {
                cluck::timeout_t const obtention_timeout(key_ticket.second->get_obtention_timeout());
                list
                    << "obtention "
                    << obtention_timeout.to_string();
            }

            list << '\n';
        }
    }
    return list.str();
}


void cluckd::election_status()
{
    // we already have election results?
    //
    if(!f_leaders.empty())
    {
        // the results may have been "temperred" with (i.e. one of
        // the leaders was lost)
        //
        if(f_leaders.size() == 3
        || (f_neighbors_count < 3 && f_leaders.size() == f_neighbors_count))
        {
            return;
        }
    }

    // we do not yet know our IP address, we cannot support election just yet
    //
    if(f_my_ip_address.is_default())
    {
        return;
    }

    // neighbors count is 0 until we receive a very first CLUSTER_UP
    // (note that it does not go back to zero on CLUSTER_DOWN, however,
    // the quorum as checked in the next if() is never going to be
    // reached if the cluster is down)
    //
    if(f_neighbors_count == 0)
    {
        return;
    }

    // this one probably looks complicated...
    //
    // if our quorum is 1 or 2 then we need a number of computers
    // equal to the total number of computers (i.e. a CLUSTER_COMPLETE
    // status which we compute here)
    //
    if(f_neighbors_quorum < 3
    && f_computers.size() < f_neighbors_count)
    {
        return;
    }

    // since the neighbors count & quorum never go back to zero (on a
    // CLUSTER_DOWN) we have to verify that the number of computers is
    // acceptable here
    //
    // Note: further we will not count computers marked disabled, which
    //       is done below when sorting by ID, however, that does not
    //       prevent the quorum to be attained, even with disabled
    //       computers
    //
    if(f_computers.size() < f_neighbors_quorum)
    {
        return;
    }

    // to proceed with an election we must have the smallest IP address
    // (it is not absolutely required, but that way we avoid many
    // consensus problems, in effect we have one "temporary-leader" that ends
    // up telling us who the final three leaders are)
    //
    for(auto & c : f_computers)
    {
        // Note: the test fails when we compare to ourselves so we do not
        //       need any special case
        //
        if(c.second->get_ip_address() < f_my_ip_address)
        {
            return;
        }
    }

    // to select the leaders sort them by identifier and take the first
    // three (i.e. lower priority, random, IP, pid.)
    //
    int off(0);
    computer::map_t sort_by_id;
    for(auto const & c : f_computers)
    {
        // ignore nodes with a priority of 15 (i.e. OFF)
        //
        if(c.second->get_priority() != computer::PRIORITY_OFF)
        {
            std::string id(c.second->get_id());

            // is this computer a leader?
            //
            auto const it(std::find(f_leaders.begin(), f_leaders.end(), c.second));
            if(it != f_leaders.end())
            {
                // leaders have a priority of 00
                //
                id[0] = '0';
                id[1] = '0';
            }

            sort_by_id[id] = c.second;
        }
        else
        {
            ++off;
        }
    }

    if(f_computers.size() <= 3)
    {
        if(off != 0)
        {
            SNAP_LOG_FATAL
                << "you cannot have any cluck computer turned OFF when you"
                   " have three or less computers total in your cluster."
                   " The elections cannot be completed in these"
                   " conditions."
                << SNAP_LOG_SEND;
            return;
        }
    }
    else if(f_computers.size() - off < 3)
    {
        SNAP_LOG_FATAL
            << "you have a total of "
            << f_computers.size()
            << " computers in your cluster. You turned off "
            << off
            << " of them, which means less than three are left"
               " as candidates for leadership which is not enough."
               " You can have a maximum of "
            << f_computers.size() - 3
            << " that are turned off on this cluster."
            << SNAP_LOG_SEND;
        return;
    }

    if(sort_by_id.size() < 3
    && sort_by_id.size() != f_computers.size())
    {
        return;
    }

//std::cerr << f_communicator_port << " is conducting an election:\n";
//for(auto s : sort_by_id)
//{
//std::cerr << "  " << s.second->get_name() << "    " << s.first << "\n";
//}

    // the first three are the new leaders
    //
    ed::message lock_leaders_message;
    lock_leaders_message.set_command(cluck::g_name_cluck_cmd_lock_leaders);
    lock_leaders_message.set_service(communicatord::g_name_communicatord_server_any);
    f_leaders.clear();
    f_election_date = snapdev::now();
    lock_leaders_message.add_parameter(cluck::g_name_cluck_param_election_date, f_election_date);
    auto leader(sort_by_id.begin());
    std::size_t const max(std::min(static_cast<computer::map_t::size_type>(3), sort_by_id.size()));
    for(std::size_t idx(0); idx < max; ++idx, ++leader)
    {
        lock_leaders_message.add_parameter(
              cluck::g_name_cluck_param_leader + std::to_string(idx)
            , leader->second->get_id());
        f_leaders.push_back(leader->second);
    }
    f_messenger->send_message(lock_leaders_message);

#if 1
SNAP_LOG_WARNING
<< "election status = add leader(s)... "
<< f_computers.size()
<< " computers and "
<< f_leaders.size()
<< " leaders."
<< SNAP_LOG_SEND;
#endif
}


void cluckd::check_lock_status()
{
    bool const ready(is_daemon_ready());
    if(f_lock_status == ready)
    {
        return;
    }
    f_lock_status = ready;

    ed::message status_message;
    status_message.set_command(f_lock_status
                    ? cluck::g_name_cluck_cmd_lock_ready
                    : cluck::g_name_cluck_cmd_no_lock);
SNAP_LOG_WARNING << "sending lock status (on a check): " << status_message.get_command() << SNAP_LOG_SEND;
    status_message.set_service(communicatord::g_name_communicatord_server_me);
    status_message.add_parameter(communicatord::g_name_communicatord_param_cache, communicatord::g_name_communicatord_value_no);
    f_messenger->send_message(status_message);

    if(ready
    && !f_message_cache.empty())
    {
        // we still have a cache of locks that can now be processed
        //
        // note:
        // although msg_lock() could re-add some of those messages
        // in the f_message_cache vector, it should not since it
        // calls the same is_daemon_ready() function which we know returns
        // true and therefore no cache is required
        //
        message_cache::list_t cache;
        cache.swap(f_message_cache);
        for(auto & mc : cache)
        {
            msg_lock(mc.f_message);
        }
    }
}


void cluckd::send_lock_started(ed::message const * msg)
{
    // tell other cluck daemon instances that are already listening that
    // we are ready; this way we can calculate the number of computers
    // available in our network and use that to calculate the QUORUM
    //
    ed::message lock_started_message;
    lock_started_message.set_command(cluck::g_name_cluck_cmd_lock_started);
    if(msg == nullptr)
    {
        lock_started_message.set_service(communicatord::g_name_communicatord_service_public_broadcast);

        // unfortunately, the following does NOT work as expected...
        // (i.e. the following ends up sending the message to ourselves only
        // and does not forward to any remote communicators).
        //
        //lock_started_message.set_server("*");
        //lock_started_message.set_service("cluckd");
    }
    else
    {
        lock_started_message.reply_to(*msg);
    }

    // our info: server name and id
    //
    lock_started_message.add_parameter(communicatord::g_name_communicatord_param_server_name, f_server_name);
    lock_started_message.add_parameter(cluck::g_name_cluck_param_lock_id, f_my_id);
    lock_started_message.add_parameter(cluck::g_name_cluck_param_start_time, f_start_time);

    // include the leaders if present
    //
    if(!f_leaders.empty())
    {
        lock_started_message.add_parameter(cluck::g_name_cluck_param_election_date, f_election_date);
        for(size_t idx(0); idx < f_leaders.size(); ++idx)
        {
            lock_started_message.add_parameter(
                  cluck::g_name_cluck_param_leader + std::to_string(idx)
                , f_leaders[idx]->get_id());
        }
    }

    f_messenger->send_message(lock_started_message);
}


/** \brief Called whenever we receive the STOP command or equivalent.
 *
 * This function makes sure the cluck daemon exits as quickly as
 * possible. This means unregistering all the daemon's connections
 * from the communicator.
 *
 * If possible, the function sends an UNREGISTER message to the
 * communicator daemon.
 *
 * \param[in] quitting  Set to true if we received a QUITTING message (false
 * usually means we received a STOP message).
 */
void cluckd::stop(bool quitting)
{
    if(f_messenger != nullptr)
    {
        f_messenger->unregister_fluid_settings(quitting);
        f_communicator->remove_connection(f_messenger);
        f_messenger.reset();
    }

    if(f_communicator != nullptr)
    {
        f_communicator->remove_connection(f_interrupt);
        f_interrupt.reset();

        f_communicator->remove_connection(f_timer);
        f_timer.reset();
    }
}


/** \brief Make sure the very first ticket is marked as LOCKED.
 *
 * This function is called whenever the f_tickets map changes
 * (more specifically, one of its children) to make sure
 * that the first ticket is clearly marked as being locked.
 * Most of the time this happens when we add and when we remove
 * tickets.
 *
 * Note that the function may be called many times even though the
 * first ticket does not actually change. Generally this is fine 
 * although each time it sends an ACTIVATE_LOCK message so we want
 * to limit the number of calls to make sure we do not send too
 * many possibly confusing messages.
 *
 * \note
 * We need the ACTIVATE_LOCK and LOCK_ACTIVATED messages to make sure
 * that we only activate the very first lock which we cannot be sure
 * of on our own because all the previous messages are using the
 * QUORUM as expected and thus our table of locks may not be complete
 * at any one time.
 *
 * \param[in] object_name  The name of the object which very
 *                         first ticket may have changed.
 */
void cluckd::activate_first_lock(std::string const & object_name)
{
    auto ticket(find_first_lock(object_name));

    if(ticket != nullptr)
    {
        // there is what we think is the first ticket
        // that should be actived now; we need to share
        // with the other 2 leaders to make sure of that
        //
        ticket->activate_lock();
    }
}


ticket::pointer_t cluckd::find_first_lock(std::string const & object_name)
{
    ticket::pointer_t first_ticket;
    auto const obj_ticket(f_tickets.find(object_name));

    if(obj_ticket != f_tickets.end())
    {
        // loop through making sure that we activate a ticket only
        // if the obtention date was not already reached; if that
        // date was reached before we had the time to activate the
        // lock, then the client should have abandonned the lock
        // request anyway...
        //
        // (this is already done in the cleanup(), but a couple of
        // other functions may call the activate_first_lock()
        // function!)
        //
        for(auto key_ticket(obj_ticket->second.begin()); key_ticket != obj_ticket->second.end(); )
        {
            if(key_ticket->second->timed_out())
            {
                // that ticket timed out, send an UNLOCKING, UNLOCKED,
                // or LOCK_FAILED message and get rid of it
                //
                key_ticket->second->lock_failed();
                if(key_ticket->second->timed_out())
                {
                    // still timed out, remove it
                    //
                    key_ticket = obj_ticket->second.erase(key_ticket);
                }
            }
            else
            {
                if(first_ticket == nullptr)
                {
                    first_ticket = key_ticket->second;
                }
                ++key_ticket;
            }
        }

        if(obj_ticket->second.empty())
        {
            // it is empty now, get rid of that set of tickets
            //
            f_tickets.erase(obj_ticket);
        }
    }

    return first_ticket;
}


/** \brief Synchronize leaders.
 *
 * This function sends various events to the other two leaders in order
 * to get them to synchronize the tickets this cluck daemon currently holds.
 *
 * Only leaders make use of this function.
 *
 * Synchronization is necessary whenever a leader dies and another gets
 * elected as a replacement. The replacement would have no idea of the
 * old tickets. This function makes sure that such doesn't occur.
 *
 * \note
 * Our test checks the validity when ONE leader is lost. If TWO of the
 * leaders are lost at once, the algorithm may not be 100% reliable.
 * Especially, the remaining leader may not have all the necessary
 * information to restore all the tickets as they were expected to be.
 *
 * \warning
 * A ticket that just arrived to a leader and was not yet forwarded to
 * the others with the LOCK_ENTERING message is going to be lost no
 * matter what.
 */
void cluckd::synchronize_leaders()
{
    // there is nothing to do if we are by ourselves because we cannot
    // gain any type of concensus unless we are expected to be the only
    // one in which case there is no synchronization requirements anyway
    //
    if(f_leaders.size() <= 1)
    {
        return;
    }

    // only leaders can synchronize each others
    // (other cluck daemons do not have any tickets to synchronize)
    //
    if(is_leader() == nullptr)
    {
        return;
    }

    // determine whether we are leader #0 or not, if zero, then we
    // call msg_lock() directly, otherwise we do a f_messenger->send_message()
    //
    bool const leader0(f_leaders[0]->get_id() == f_my_id);

    // a vector of messages for which we have to call msg_lock()
    //
    ed::message::vector_t local_locks;

    // if entering a ticket is definitely not locked, although it
    // could be ready (one step away from being locked!) we still
    // restart the whole process with the new leaders if such
    // exist
    //
    // Note: of course we restart the process only if the owner
    //       was that one leader that disappeared, not if the
    //       ticket is owned by a remaining leader
    //
    for(auto obj_entering(f_entering_tickets.begin()); obj_entering != f_entering_tickets.end(); ++obj_entering)
    {
        for(auto key_entering(obj_entering->second.begin()); key_entering != obj_entering->second.end(); )
        {
            std::string const owner_name(key_entering->second->get_owner());
            auto key_leader(std::find_if(
                      f_leaders.begin()
                    , f_leaders.end()
                    , [&owner_name](auto const & l)
                    {
                        return l->get_name() == owner_name;
                    }));
            if(key_leader == f_leaders.end())
            {
                // give new ownership to leader[0]
                //
                ed::message lock_message;
                lock_message.set_command(cluck::g_name_cluck_cmd_lock);
                lock_message.set_server(f_leaders[0]->get_name());
                lock_message.set_service(cluck::g_name_cluck_service_name);
                lock_message.set_sent_from_server(key_entering->second->get_server_name());
                lock_message.set_sent_from_service(key_entering->second->get_service_name());
                lock_message.add_parameter(cluck::g_name_cluck_param_object_name, key_entering->second->get_object_name());
                lock_message.add_parameter(cluck::g_name_cluck_param_tag, key_entering->second->get_tag());
                lock_message.add_parameter(cluck::g_name_cluck_param_pid, key_entering->second->get_client_pid());
                lock_message.add_parameter(cluck::g_name_cluck_param_timeout, key_entering->second->get_obtention_timeout());
                lock_message.add_parameter(cluck::g_name_cluck_param_duration, key_entering->second->get_lock_duration());
                lock_message.add_parameter(cluck::g_name_cluck_param_unlock_duration, key_entering->second->get_unlock_duration());
                if(leader0)
                {
                    // we are leader #0 so directly call msg_lock()
                    //
                    // first we remove the entry otherwise we get a duplicate
                    // error since we try to readd the same ticket
                    //
                    key_entering = obj_entering->second.erase(key_entering);
                    local_locks.push_back(lock_message);
                }
                else
                {
                    // we are not leader #0, so send the message to it
                    //
                    ++key_entering;
                    lock_message.add_parameter(cluck::g_name_cluck_param_serial, key_entering->second->get_serial());
                    f_messenger->send_message(lock_message);
                }
            }
            else
            {
                ++key_entering;
            }
        }
    }

    // a ticket may still be unlocked in which case we want to
    // restart the lock process as if still entering
    //
    // if locked, a ticket is assigned leader0 as its new owner so
    // further work on that ticket works as expected
    //
    std::string serialized;
    for(auto obj_ticket(f_tickets.begin()); obj_ticket != f_tickets.end(); ++obj_ticket)
    {
        for(auto key_ticket(obj_ticket->second.begin()); key_ticket != obj_ticket->second.end(); )
        {
            std::string const owner_name(key_ticket->second->get_owner());
            auto key_leader(std::find_if(
                      f_leaders.begin()
                    , f_leaders.end()
                    , [&owner_name](auto const & l)
                    {
                        return l->get_name() == owner_name;
                    }));
            if(key_ticket->second->is_locked())
            {
                // if ticket was locked by the leader that disappeared, we
                // transfer ownership to the new leader #0
                //
                if(key_leader == f_leaders.end())
                {
                    key_ticket->second->set_owner(f_leaders[0]->get_name());
                }

                // and send that ticket to the other leaders to make sure
                // they all agree on its current state
                //
                serialized += key_ticket->second->serialize();
                serialized += '\n';

                ++key_ticket;
            }
            else
            {
                // it was not locked yet, restart the LOCK process from
                // the very beginning
                //
                if(key_leader == f_leaders.end())
                {
                    // give new ownership to leader[0]
                    //
                    ed::message lock_message;
                    lock_message.set_command(cluck::g_name_cluck_cmd_lock);
                    lock_message.set_server(f_leaders[0]->get_name());
                    lock_message.set_service(cluck::g_name_cluck_service_name);
                    lock_message.set_sent_from_server(key_ticket->second->get_server_name());
                    lock_message.set_sent_from_service(key_ticket->second->get_service_name());
                    lock_message.add_parameter(cluck::g_name_cluck_param_object_name, key_ticket->second->get_object_name());
                    lock_message.add_parameter(cluck::g_name_cluck_param_tag, key_ticket->second->get_tag());
                    lock_message.add_parameter(cluck::g_name_cluck_param_pid, key_ticket->second->get_client_pid());
                    lock_message.add_parameter(cluck::g_name_cluck_param_timeout, key_ticket->second->get_obtention_timeout());
                    lock_message.add_parameter(cluck::g_name_cluck_param_duration, key_ticket->second->get_lock_duration());
                    lock_message.add_parameter(cluck::g_name_cluck_param_unlock_duration, key_ticket->second->get_unlock_duration());
                    if(leader0)
                    {
                        // we are leader #0 so directly call msg_lock()
                        //
                        key_ticket = obj_ticket->second.erase(key_ticket);
                        local_locks.push_back(lock_message);
                    }
                    else
                    {
                        // we are not leader #0, so send the message to it
                        //
                        ++key_ticket;
                        lock_message.add_parameter(cluck::g_name_cluck_param_serial, key_ticket->second->get_serial());
                        f_messenger->send_message(lock_message);
                    }
                }
                else
                {
                    ++key_ticket;
                }
            }
        }
    }

    // we send those after the loops above because the msg_lock() is
    // not unlikely to change the f_entering_tickets map and looping
    // through it when another function is going to modify it is not
    // wise
    //
    for(auto lm : local_locks)
    {
        msg_lock(lm);
    }

    // send LOCK_TICkETS if there is serialized ticket data
    //
    if(!serialized.empty())
    {
        ed::message lock_tickets_message;
        lock_tickets_message.set_command(cluck::g_name_cluck_cmd_lock_tickets);
        lock_tickets_message.set_service(cluck::g_name_cluck_service_name);
        lock_tickets_message.add_parameter(cluck::g_name_cluck_param_tickets, serialized);

        auto const la(get_leader_a());
        if(la != nullptr)
        {
            lock_tickets_message.set_server(la->get_name());
            f_messenger->send_message(lock_tickets_message);

            auto const lb(get_leader_b());
            if(lb != nullptr)
            {
                lock_tickets_message.set_server(lb->get_name());
                f_messenger->send_message(lock_tickets_message);
            }
        }
    }
}


/** \brief Forward a user message to a leader.
 *
 * The user may send a LOCK or an UNLOCK command to the cluck daemon.
 * Those messages need to be forwarded to a leader to work as expected.
 * If we are not a leader, then we need to call this function to
 * forward the message (this daemon acts as a proxy).
 *
 * Note that we do not make a copy of the message because we do not expect
 * it to be used any further after this call so we may as well update that
 * message. It should not be destructive at all anyway.
 *
 * \param[in,out] msg  The message being forwarded to a leader.
 */
void cluckd::forward_message_to_leader(ed::message & msg)
{
    // we are not a leader, we work as a proxy by forwarding the
    // message to a leader, we add our trail so the LOCKED and
    // other messages can be proxied back
    //
    // Note: using the get_sent_from_server() means that we may not
    //       even see the returned message, it may be proxied to another
    //       server directly or through another route
    //
    msg.set_service(cluck::g_name_cluck_service_name);
    msg.add_parameter(cluck::g_name_cluck_param_lock_proxy_server_name, msg.get_sent_from_server());
    msg.add_parameter(cluck::g_name_cluck_param_lock_proxy_service_name, msg.get_sent_from_service());

    f_next_leader = (f_next_leader + 1) % f_leaders.size();
    msg.set_server(f_leaders[f_next_leader]->get_name());

    f_messenger->send_message(msg);
}


/** \brief Clean timed out entries if any.
 *
 * This function goes through the list of tickets and entering
 * entries and removes any one of them that timed out. This is
 * important if a process dies and does not properly remove
 * its locks.
 *
 * When the timer gets its process_timeout() function called,
 * it ends up calling this function to clean up any lock that
 * has timed out.
 */
void cluckd::cleanup()
{
    cluck::timeout_t next_timeout(snapdev::timespec_ex::max());

    // when we receive LOCK requests before we have leaders elected, they
    // get added to our cache, so do some cache clean up when not empty
    //
    cluck::timeout_t const now(snapdev::now());
    for(auto c(f_message_cache.begin()); c != f_message_cache.end(); )
    {
        if(c->f_timeout <= now)
        {
            std::string object_name;
            ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
            pid_t client_pid(0);
            cluck::timeout_t timeout;
            if(!get_parameters(c->f_message, &object_name, &tag, &client_pid, &timeout, nullptr, nullptr))
            {
                // we should never cache messages that are invalid
                //
                throw cluck::logic_error("cluck::cleanup() of LOCK message failed get_parameters()."); // LCOV_EXCL_LINE
            }

            SNAP_LOG_WARNING
                << "Lock on \""
                << object_name
                << "\" / \""
                << client_pid
                << "\" timed out before leaders were known."
                << SNAP_LOG_SEND;

            std::string const server_name(c->f_message.has_parameter("lock_proxy_server_name")
                                        ? c->f_message.get_parameter("lock_proxy_server_name")
                                        : c->f_message.get_sent_from_server());
            std::string const entering_key(server_name + '/' + std::to_string(client_pid));

            ed::message lock_failed_message;
            lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
            lock_failed_message.reply_to(c->f_message);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, entering_key);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_timedout);
            f_messenger->send_message(lock_failed_message);

            c = f_message_cache.erase(c);
        }
        else
        {
            if(c->f_timeout < next_timeout)
            {
                next_timeout = c->f_timeout;
            }
            ++c;
        }
    }

    // remove any f_ticket that timed out
    //
    for(auto obj_ticket(f_tickets.begin()); obj_ticket != f_tickets.end(); )
    {
        bool try_activate(false);
        for(auto key_ticket(obj_ticket->second.begin()); key_ticket != obj_ticket->second.end(); )
        {
            bool move_next(true);
            if(key_ticket->second->timed_out())
            {
                key_ticket->second->lock_failed();
                if(key_ticket->second->timed_out())
                {
                    // still timed out, remove it
                    //
                    key_ticket = obj_ticket->second.erase(key_ticket);
                    try_activate = true;
                    move_next = false;
                }
            }
            if(move_next)
            {
                if(key_ticket->second->get_current_timeout_date() < next_timeout)
                {
                    next_timeout = key_ticket->second->get_current_timeout_date();
                }
                ++key_ticket;
            }
        }

        if(obj_ticket->second.empty())
        {
            obj_ticket = f_tickets.erase(obj_ticket);
        }
        else
        {
            if(try_activate)
            {
                // something was erased, a new ticket may be first
                //
                activate_first_lock(obj_ticket->first);
            }

            ++obj_ticket;
        }
    }

    // remove any f_entering_tickets that timed out
    //
    for(auto obj_entering(f_entering_tickets.begin()); obj_entering != f_entering_tickets.end(); )
    {
        for(auto key_entering(obj_entering->second.begin()); key_entering != obj_entering->second.end(); )
        {
            if(key_entering->second->timed_out())
            {
                key_entering->second->lock_failed();
                if(key_entering->second->timed_out())
                {
                    // still timed out, remove it
                    //
                    key_entering = obj_entering->second.erase(key_entering);
                }
            }
            else
            {
                if(key_entering->second->get_current_timeout_date() < next_timeout)
                {
                    next_timeout = key_entering->second->get_current_timeout_date();
                }
                ++key_entering;
            }
        }

        if(obj_entering->second.empty())
        {
            obj_entering = f_entering_tickets.erase(obj_entering);
        }
        else
        {
            ++obj_entering;
        }
    }

    // got a new timeout?
    //
    if(next_timeout != snapdev::timespec_ex::max())
    {
        // we add one second to avoid looping like crazy
        // if we timeout just around the "wrong" time
        //
        f_timer->set_timeout_date(next_timeout + cluck::timeout_t(1, 0));
    }
    else
    {
        f_timer->set_timeout_date(-1);
    }
}


/** \brief Determine the last ticket defined in this cluck daemon.
 *
 * This function loops through the existing tickets and returns the
 * largest ticket number it finds.
 *
 * Note that the number returned is the last ticket. At some point,
 * the caller needs to add one to this number before assigning the
 * result to a new ticket.
 *
 * If no ticket were defined for \p object_name or we are dealing with
 * that object very first ticket, then the function returns NO_TICKET
 * (which is 0).
 *
 * \param[in] object_name  The name of the object for which the last ticket
 *                         number is being sought.
 *
 * \return The last ticket number or NO_TICKET.
 */
ticket::ticket_id_t cluckd::get_last_ticket(std::string const & object_name)
{
    ticket::ticket_id_t last_ticket(ticket::NO_TICKET);

    // Note: There is no need to check the f_entering_tickets list
    //       since that one does not yet have any ticket number assigned
    //       and thus the maximum there would return 0 every time
    //
    auto obj_ticket(f_tickets.find(object_name));
    if(obj_ticket != f_tickets.end())
    {
        // note:
        // the std::max_element() algorithm would require many more
        // get_ticket_number() when our loop uses one per ticket
        //
        for(auto key_ticket : obj_ticket->second)
        {
            ticket::ticket_id_t const ticket_number(key_ticket.second->get_ticket_number());
            if(ticket_number > last_ticket)
            {
                last_ticket = ticket_number;
            }
        }
    }

    return last_ticket;
}


/** \brief Set the ticket.
 *
 * Once a ticket was assigned a valid identifier (see get_last_ticket())
 * it can be saved as a ticket. This function does that. Now this is
 * an official ticket.
 *
 * \param[in] object_name  The name of the object being locked.
 * \param[in] key  The ticket key (3 segments).
 * \param[in] ticket  The ticket object being added.
 */
void cluckd::set_ticket(
      std::string const & object_name
    , std::string const & key
    , ticket::pointer_t ticket)
{
    f_tickets[object_name][key] = ticket;
}


/** \brief Get a reference to the list of entering tickets.
 *
 * This function returns a constant reference to the list of entering
 * tickets. This is used by the ticket::add_ticket() function
 * in order to know once all entering tickets are done so the algorithm
 * can move forward.
 *
 * \param[in] object_name  The name of the object being locked.
 *
 * \return A constant copy of the list of entering tickets.
 */
ticket::key_map_t const cluckd::get_entering_tickets(std::string const & object_name)
{
    auto const it(f_entering_tickets.find(object_name));
    if(it == f_entering_tickets.end())
    {
        return ticket::key_map_t();
    }

    return it->second;
}


/** \brief Used to simulate a LOCK_EXITING message.
 *
 * This function is called to simulate sending a LOCK_EXITING to the
 * cluckd object from the ticket object.
 *
 * \param[in] msg  The LOCK_EXITING message with proper object name, tag,
 * and key.
 */
void cluckd::lock_exiting(ed::message & msg)
{
    msg_lock_exiting(msg);
}






std::string cluckd::serialized_tickets()
{
    std::stringstream result;

    for(auto const & obj_ticket : f_tickets)
    {
        for(auto const & key_ticket : obj_ticket.second)
        {
            result
                << key_ticket.second->serialize()
                << '\n';
        }
    }

    return result.str();
}


/** \brief Try to get a set of parameters.
 *
 * This function attempts to get the specified set of parameters from the
 * specified message.
 *
 * The function throws if a parameter is missing or invalid (i.e. passed
 * a string when an integer was expected).
 *
 * When defined, the \p client_pid parameter is expected to be a positive
 * integer. Any other number makes the function emit an error and return
 * false.
 *
 * \note
 * The timeout parameter is always viewed as optional. It is set to
 * "now + cluck::CLUCK_LOCK_DEFAULT_TIMEOUT" if undefined in the message.
 * If specified in the message, there is no minimum or maximum
 * (i.e. it may already have timed out).
 *
 * \param[in] message  The message from which we get parameters.
 * \param[out] object_name  A pointer to an std::string that receives the object name.
 * \param[out] tag  A pointer to a tag_t that receives the tag number.
 * \param[out] client_pid  A pointer to a pid_t that receives the client pid.
 * \param[out] timeout  A pointer to an cluck::timeout_t that receives the timeout date.
 * \param[out] key  A pointer to an std::string that receives the key parameter.
 * \param[out] source  A pointer to an std::string that receives the source parameter.
 *
 * \return true if all specified parameters could be retrieved as expected,
 * false if parameters are either missing or invalid.
 */
bool cluckd::get_parameters(
      ed::message const & msg
    , std::string * object_name
    , ed::dispatcher_match::tag_t * tag
    , pid_t * client_pid
    , cluck::timeout_t * timeout
    , std::string * key
    , std::string * source)
{
    // get the "object name" (what we are locking)
    // in Snap, the object name is often a URI plus the action we are performing
    //
    if(object_name != nullptr)
    {
        *object_name = msg.get_parameter(cluck::g_name_cluck_param_object_name);
    }

    // the same application may want to hold multiple locks simultaneously
    // and this is made possible by using a tag (a 16 bits number)
    //
    if(tag != nullptr)
    {
        *tag = msg.get_integer_parameter(cluck::g_name_cluck_param_tag);
    }

    // get the pid (process identifier) of the process that is
    // requesting the lock; this is important to be able to distinguish
    // multiple processes on the same computer requesting a lock
    //
    if(client_pid != nullptr)
    {
        *client_pid = msg.get_integer_parameter(cluck::g_name_cluck_param_pid);
        if(*client_pid < 1)
        {
            // invalid pid
            //
            SNAP_LOG_NOISY_ERROR
                << "cluckd::get_parameters(): invalid pid specified for a lock ("
                << std::to_string(*client_pid)
                << "); it must be a positive decimal number."
                << SNAP_LOG_SEND;
            return false;
        }
    }

    // get the time limit we will wait up to before we decide we
    // cannot obtain that lock
    //
    if(timeout != nullptr)
    {
        if(msg.has_parameter(cluck::g_name_cluck_param_timeout))
        {
            // this timeout may already be out of date in which case
            // the lock immediately fails
            //
            *timeout = msg.get_timespec_parameter(cluck::g_name_cluck_param_timeout);
        }
        else
        {
            *timeout = snapdev::now() + cluck::CLUCK_UNLOCK_DEFAULT_TIMEOUT;
        }
    }

    // get the key of a ticket or entering object
    //
    if(key != nullptr)
    {
        *key = msg.get_parameter(cluck::g_name_cluck_param_key);
    }

    // get the source of a ticket (i.e. <server> '/' <service>)
    //
    if(source != nullptr)
    {
        *source = msg.get_parameter(cluck::g_name_cluck_param_source);
    }

    return true;
}


/** \brief Lock the resource after confirmation that client is alive.
 *
 * This message is expected just after we sent an ALIVE message to
 * the client.
 *
 * Whenever a leader dies, we suspect that the client may have died
 * with it so we send it an ALIVE message to know whether it is worth
 * the trouble of entering that lock.
 *
 * \param[in] msg  The ABSOLUTELY message to handle.
 */
void cluckd::msg_absolutely(ed::message & msg)
{
    // we may receive the ABSOLUTELY message from anywhere so don't expect
    // that the serial parameter is going to be defined
    //
    if(!msg.has_parameter(ed::g_name_ed_param_serial))
    {
        return;
    }

    std::string const serial(msg.get_parameter(ed::g_name_ed_param_serial));
    std::vector<std::string> segments;
    snapdev::tokenize_string(segments, serial, "/");

    if(segments[0] == "relock")
    {
        // check serial as defined in msg_lock()
        // alive_message.add_parameter("serial", "relock/" + object_name + '/' + entering_key);
        //
        if(segments.size() != 4)
        {
            SNAP_LOG_WARNING
                << "ABSOLUTELY reply has an invalid relock serial parameters \""
                << serial
                << "\" was expected to have exactly 4 segments."
                << SNAP_LOG_SEND;

            // this would not work without an object name & tag
            // use an INVALID message instead
            //ed::message lock_failed_message;
            //lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
            //lock_failed_message.reply_to(msg);
            //lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, cluck::g_name_cluck_value_unknown);
            //lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, cluck::g_name_cluck_value_unknown);
            //lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, serial);
            //lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
            //f_messenger->send_message(lock_failed_message);

            ed::message invalid;
            invalid.set_command(ed::g_name_ed_cmd_invalid);
            invalid.reply_to(msg);
            invalid.add_parameter(
                  ed::g_name_ed_param_command
                , msg.get_command());
            invalid.add_parameter(
                  ed::g_name_ed_param_message
                , "invalid number of segments in \""
                + serial
                + "\".");
            f_messenger->send_message(invalid);

            return;
        }

        // notice how the split() re-split the entering key
        //
        std::string const object_name(segments[1]);
        std::string const server_name(segments[2]);
        std::string const client_pid(segments[3]);

        auto entering_ticket(f_entering_tickets.find(object_name));
        if(entering_ticket != f_entering_tickets.end())
        {
            std::string const entering_key(server_name + '/' + client_pid);
            auto key_ticket(entering_ticket->second.find(entering_key));
            if(key_ticket != entering_ticket->second.end())
            {
                // remove the alive timeout
                //
                key_ticket->second->set_alive_timeout(cluck::timeout_t());

                // got it! start the bakery algorithm
                //
                key_ticket->second->entering();
            }
        }
    }

    // ignore other messages
}


/** \brief Acknowledge the ACTIVATE_LOCK with what we think is our first lock.
 *
 * This function replies to an ACTIVATE_LOCK request with what we think is
 * the first lock for the specified object.
 *
 * Right now, we disregard the specified key. There is nothing we can really
 * do with it here.
 *
 * If we do not have a ticket for the specified object (something that could
 * happen if the ticket just timed out) then we still have to reply, only
 * we let the other leader know that we have no clue what he is talking about.
 *
 * \param[in] msg  The ACTIVATE_LOCK message.
 */
void cluckd::msg_activate_lock(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    std::string first_key("no-key");

    auto ticket(find_first_lock(object_name));
    if(ticket != nullptr)
    {
        // found a lock
        //
        first_key = ticket->get_ticket_key();

        if(key == first_key)
        {
            // we can mark this ticket as activated
            //
            ticket->lock_activated();
        }
    }

    // always reply, if we could not find the key, then we returned 'no-key'
    // as the key parameter
    //
    ed::message lock_activated_message;
    lock_activated_message.set_command(cluck::g_name_cluck_cmd_lock_activated);
    lock_activated_message.reply_to(msg);
    lock_activated_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
    lock_activated_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
    lock_activated_message.add_parameter(cluck::g_name_cluck_param_key, key);
    lock_activated_message.add_parameter(cluck::g_name_cluck_param_other_key, first_key);
    f_messenger->send_message(lock_activated_message);

    // the list of tickets is not unlikely changed so we need to make
    // a call to cleanup to make sure the timer is reset appropriately
    //
    cleanup();
}


/** \brief Add a ticket from another cluckd.
 *
 * Tickets get duplicated on the cluckd leaders.
 *
 * \note
 * Although we only need a QUORUM number of nodes to receive a copy of
 * the data, the data still get broadcast to all the cluck leaders.
 * After this message arrives any one of the cluck process can
 * handle the unlock if the UNLOCK message gets sent to another process
 * instead of the one which first created the ticket. This is the point
 * of the implementation since we want to be fault tolerant (as in if one
 * of the leaders goes down, the locking mechanism still works).
 *
 * \param[in] msg  The ADD_TICKET message being handled.
 */
void cluckd::msg_add_ticket(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    cluck::timeout_t timeout;
    if(!get_parameters(msg, &object_name, &tag, nullptr, &timeout, &key, nullptr))
    {
        return;
    }

#ifdef _DEBUG
    {
        auto const obj_ticket(f_tickets.find(object_name));
        if(obj_ticket != f_tickets.end())
        {
            auto const key_ticket(obj_ticket->second.find(key));
            if(key_ticket != obj_ticket->second.end())
            {
                // this ticket exists on this system
                //
                throw cluck::logic_error("cluck::add_ticket() ticket already exists");
            }
        }
    }
#endif

    // the client_pid parameter is part of the key (3rd segment)
    //
    std::vector<std::string> segments;
    snapdev::tokenize_string(segments, key, "/");
    if(segments.size() != 3)
    {
        SNAP_LOG_ERROR
            << "Expected exactly 3 segments in \""
            << key
            << "\" to add a ticket."
            << SNAP_LOG_SEND;

        ed::message lock_failed_message;
        lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
        lock_failed_message.reply_to(msg);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, key);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
        f_messenger->send_message(lock_failed_message);

        return;
    }

    // TODO: we probably want to look at using a function which returns false
    //       instead of having to do a try/catch
    //
    bool ok(true);
    std::uint32_t number(0);
    try
    {
        number = snapdev::hex_to_int<std::uint32_t>(segments[0]);
    }
    catch(snapdev::hexadecimal_string_exception const &)
    {
        ok = false;
    }
    catch(snapdev::hexadecimal_string_out_of_range const &)
    {
        ok = false;
    }
    if(!ok)
    {
        SNAP_LOG_ERROR
            << "somehow ticket number \""
            << segments[0]
            << "\" is not a valid hexadecimal number."
            << SNAP_LOG_SEND;

        ed::message lock_failed_message;
        lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
        lock_failed_message.reply_to(msg);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, key);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
        f_messenger->send_message(lock_failed_message);

        return;
    }

    // by now all the leaders should already have
    // an entering ticket for that one ticket
    //
    auto const obj_entering_ticket(f_entering_tickets.find(object_name));
    if(obj_entering_ticket == f_entering_tickets.end())
    {
        SNAP_LOG_ERROR
            << "Expected entering ticket object for \""
            << object_name
            << "\" not found when adding a ticket."
            << SNAP_LOG_SEND;

        ed::message lock_failed_message;
        lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
        lock_failed_message.reply_to(msg);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, key);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
        f_messenger->send_message(lock_failed_message);

        return;
    }

    // the key we need to search is not the new ticket key but the
    // entering key, build it from the segments
    //
    std::string const entering_key(segments[1] + '/' + segments[2]);
    auto const key_entering_ticket(obj_entering_ticket->second.find(entering_key));
    if(key_entering_ticket == obj_entering_ticket->second.end())
    {
        SNAP_LOG_ERROR
            << "Expected entering ticket key for \""
            << object_name
            << "\" not found when adding a ticket."
            << SNAP_LOG_SEND;

        ed::message lock_failed_message;
        lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
        lock_failed_message.reply_to(msg);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, key);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
        f_messenger->send_message(lock_failed_message);

        return;
    }

    // make it an official ticket now
    //
    // this should happen on all cluck daemon other than the one that
    // first received the LOCK message
    //
    set_ticket(object_name, key, key_entering_ticket->second);

    // WARNING: the set_ticket_number() function has the same side
    //          effects as the add_ticket() function without the
    //          f_messenger->send_message() call
    //
    f_tickets[object_name][key]->set_ticket_number(number);

    ed::message ticket_added_message;
    ticket_added_message.set_command(cluck::g_name_cluck_cmd_ticket_added);
    ticket_added_message.reply_to(msg);
    ticket_added_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
    ticket_added_message.add_parameter(cluck::g_name_cluck_param_key, key);
    ticket_added_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
    f_messenger->send_message(ticket_added_message);
}


/** \brief The communicatord lost too many connections.
 *
 * When the cluster is not complete, the CLUSTER_DOWN message gets sent
 * by the communicatord. We need to stop offering locks at that point.
 * Locks that are up are fine, but new locks are not possible.
 *
 * \param[in] msg  The CLUSTER_DOWN message.
 */
void cluckd::msg_cluster_down(ed::message & msg)
{
    snapdev::NOT_USED(msg);

    SNAP_LOG_INFO
        << "cluster is down, canceling existing locks and we have to"
           " refuse any further lock requests for a while."
        << SNAP_LOG_SEND;

    // in this case, we cannot safely keep the leaders
    //
    f_leaders.clear();

    // in case services listen to the NO_LOCK, let them know it's gone
    //
    check_lock_status();

    // we do not call the lock_gone() because the HANGUP will be sent
    // if required so we do not have to do that twice
}


/** \brief Cluster is ready, send the LOCK_STARTED message.
 *
 * Our cluster is finally ready, so we can send the LOCK_STARTED and work
 * on a leader election if still required.
 */
void cluckd::msg_cluster_up(ed::message & msg)
{
    f_neighbors_count = msg.get_integer_parameter("neighbors_count");
    f_neighbors_quorum = f_neighbors_count / 2 + 1;

    computer::priority_t priority(computer::PRIORITY_OFF);
    std::string candidate_priority(f_opts.get_string("candidate-priority"));
    if(candidate_priority != "off")
    {
        priority = f_opts.get_long("candidate-priority"
                                , 0
                                , computer::PRIORITY_USER_MIN
                                , computer::PRIORITY_MAX);
    }
    //else if(f_config.has_parameter("candidate_priority"))
    //{
    //    std::string const candidate_priority(f_config["candidate_priority"]);
    //    if(candidate_priority == "off")
    //    {
    //        // a priority 15 means that this computer is not a candidate
    //        // at all (useful for nodes that get dynamically added
    //        // and removed--i.e. avoid re-election each time that happens.)
    //        //
    //        priority = computer::PRIORITY_OFF;
    //    }
    //    else
    //    {
    //        bool ok(false);
    //        priority = candidate_priority.toLong(&ok, 10);
    //        if(!ok)
    //        {
    //            SNAP_LOG_FATAL("invalid candidate_priority, a valid decimal number was expected instead of \"")(candidate_priority)("\".");
    //            exit(1);
    //        }
    //        if(priority < computer::PRIORITY_USER_MIN
    //        || priority > computer::PRIORITY_MAX)
    //        {
    //            SNAP_LOG_FATAL("candidate_priority must be between 1 and 15, \"")(candidate_priority)("\" is not valid.");
    //            exit(1);
    //        }
    //    }
    //}

    // add ourselves to the list of computers; mark us connected; get our ID
    //
    f_my_ip_address = f_messenger->get_my_address();
    f_computers[f_server_name] = std::make_shared<computer>(f_server_name, priority, f_my_ip_address);
    f_computers[f_server_name]->set_start_time(f_start_time);
    f_computers[f_server_name]->set_connected(true);
    f_my_id = f_computers[f_server_name]->get_id();

    SNAP_LOG_INFO
        << "cluster is up with "
        << f_neighbors_count
        << " neighbors, attempt an election"
           " then check for leaders by sending a LOCK_STARTED message."
        << SNAP_LOG_SEND;

    election_status();
    send_lock_started(nullptr);
}


/** \brief One of the cluckd processes asked for a ticket to be dropped.
 *
 * This function searches for the specified ticket and removes it from
 * this cluckd.
 *
 * If the specified ticket does not exist, nothing happens.
 *
 * \warning
 * The DROP_TICKET event includes either the ticket key (if available)
 * or the entering key (when the ticket key was not yet available).
 * Note that the ticket key should always exists by the time a DROP_TICKET
 * happens, but just in case this allows the dropping of a ticket at any
 * time.
 *
 * \param[in] msg  The DROP_TICKET message.
 */
void cluckd::msg_drop_ticket(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    std::vector<std::string> segments;
    snapdev::tokenize_string(segments, key, "/");

    // drop the regular ticket
    //
    // if we have only 2 segments, then there is no corresponding ticket
    // since tickets are added only once we have a ticket_id
    //
    std::string entering_key;
    if(segments.size() == 3)
    {
        auto obj_ticket(f_tickets.find(object_name));
        if(obj_ticket != f_tickets.end())
        {
            auto key_ticket(obj_ticket->second.find(key));
            if(key_ticket != obj_ticket->second.end())
            {
                obj_ticket->second.erase(key_ticket);
            }

            if(obj_ticket->second.empty())
            {
                f_tickets.erase(obj_ticket);
            }

            // one ticket was erased, another may be first now
            //
            activate_first_lock(object_name);
        }

        // we received the ticket_id in the message, so
        // we have to regenerate the entering_key without
        // the ticket_id (which is the first element)
        //
        entering_key = segments[1] + '/' + segments[2];
    }
    else
    {
        // we received the entering_key in the message, use as is
        //
        entering_key = key;
    }

    // drop the entering ticket
    //
    auto obj_entering_ticket(f_entering_tickets.find(object_name));
    if(obj_entering_ticket != f_entering_tickets.end())
    {
        auto key_entering_ticket(obj_entering_ticket->second.find(entering_key));
        if(key_entering_ticket != obj_entering_ticket->second.end())
        {
            obj_entering_ticket->second.erase(key_entering_ticket);
        }

        if(obj_entering_ticket->second.empty())
        {
            f_entering_tickets.erase(obj_entering_ticket);
        }
    }

    // the list of tickets is not unlikely changed so we need to make
    // a call to cleanup to make sure the timer is reset appropriately
    //
    cleanup();
}


/** \brief Search for the largest ticket.
 *
 * This function searches the list of tickets for the largest one
 * and returns that number.
 *
 * \param[in] msg  The message just received.
 *
 * \return The largest ticket number that currently exist in the list
 *         of tickets.
 */
void cluckd::msg_get_max_ticket(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    // remove any f_tickets that timed out by now because these should
    // not be taken in account in the max. computation
    //
    cleanup();

    ticket::ticket_id_t const last_ticket(get_last_ticket(object_name));

    ed::message reply;
    reply.set_command(cluck::g_name_cluck_cmd_max_ticket);
    reply.reply_to(msg);
    reply.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
    reply.add_parameter(cluck::g_name_cluck_param_key, key);
    reply.add_parameter(cluck::g_name_cluck_param_tag, tag);
    reply.add_parameter(cluck::g_name_cluck_param_ticket_id, last_ticket);
    f_messenger->send_message(reply);
}


/** \brief Reply to the LIST_TICKETS message with the TICKET_LIST.
 *
 * This function gets called whenever the command line tool (`cluckd`)
 * is run with the `--list` command line option. It generates a list of
 * tickets and sends that back to the tool as a TICKET_LIST message.
 *
 * \param[in] msg  The LIST_TICKETS message.
 */
void cluckd::msg_list_tickets(ed::message & msg)
{
    ed::message list_message;
    list_message.set_command(cluck::g_name_cluck_cmd_ticket_list);
    list_message.reply_to(msg);
    list_message.add_parameter(cluck::g_name_cluck_param_list, ticket_list());
    f_messenger->send_message(list_message);
}


/** \brief Lock the named resource.
 *
 * This function locks the specified resource \p object_name. It returns
 * when the resource is locked or when the lock timeout is reached.
 *
 * See the ticket class for more details about the locking
 * mechanisms (algorithm and MSC implementation).
 *
 * Note that if lock() is called with an empty string then the function
 * unlocks the lock and returns immediately with false. This is equivalent
 * to calling unlock().
 *
 * \note
 * The function reloads all the parameters (outside of the table) because
 * we need to support a certain amount of dynamism. For example, an
 * administrator may want to add a new host on the system. In that case,
 * the list of host changes and it has to be detected here.
 *
 * \attention
 * The function accepts a "serial" parameter in the message. This is only
 * used internally when a leader is lost and a new one is assigned a lock
 * which would otherwise fail.
 *
 * \warning
 * The object name is left available in the lock table. Do not use any
 * secure/secret name/word, etc. as the object name.
 *
 * \bug
 * At this point there is no proper protection to recover from errors
 * that would happen while working on locking this entry. This means
 * failures may result in a lock that never ends.
 *
 * \param[in] msg  The lock message.
 *
 * \return true if the lock was successful, false otherwise.
 *
 * \sa unlock()
 */
void cluckd::msg_lock(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    pid_t client_pid(0);
    cluck::timeout_t timeout;
    if(!get_parameters(msg, &object_name, &tag, &client_pid, &timeout, nullptr, nullptr))
    {
        return;
    }

    // do some cleanup as well
    //
    cleanup();

    // if we are a leader, create an entering key
    //
    std::string const server_name(msg.has_parameter(cluck::g_name_cluck_param_lock_proxy_server_name)
                                ? msg.get_parameter(cluck::g_name_cluck_param_lock_proxy_server_name)
                                : msg.get_sent_from_server());

    std::string const service_name(msg.has_parameter(cluck::g_name_cluck_param_lock_proxy_service_name)
                                ? msg.get_parameter(cluck::g_name_cluck_param_lock_proxy_service_name)
                                : msg.get_sent_from_service());

    std::string const entering_key(server_name + '/' + std::to_string(client_pid));

    if(timeout <= snapdev::now())
    {
        SNAP_LOG_WARNING
            << "Lock on \""
            << object_name
            << "\" ("
            << tag
            << ")/ \""
            << client_pid
            << "\" timed out before we could start the locking process."
            << SNAP_LOG_SEND;

        ed::message lock_failed_message;
        lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
        lock_failed_message.reply_to(msg);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, entering_key);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_timedout);
        f_messenger->send_message(lock_failed_message);

        return;
    }

    cluck::timeout_t const duration(msg.get_timespec_parameter(cluck::g_name_cluck_param_duration));
    if(duration < cluck::CLUCK_MINIMUM_TIMEOUT)
    {
        // duration too small
        //
        SNAP_LOG_ERROR
            << duration
            << " is an invalid duration, the minimum accepted is "
            << cluck::CLUCK_MINIMUM_TIMEOUT
            << '.'
            << SNAP_LOG_SEND;

        ed::message lock_failed_message;
        lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
        lock_failed_message.reply_to(msg);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, entering_key);
        lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
        f_messenger->send_message(lock_failed_message);

        return;
    }

    cluck::timeout_t unlock_duration(cluck::CLUCK_DEFAULT_TIMEOUT);
    if(msg.has_parameter(cluck::g_name_cluck_param_unlock_duration))
    {
        unlock_duration = msg.get_timespec_parameter(cluck::g_name_cluck_param_unlock_duration);
        if(unlock_duration < cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT)
        {
            // invalid duration, minimum is cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT
            //
            SNAP_LOG_ERROR
                << unlock_duration
                << " is an invalid unlock duration, the minimum accepted is "
                << cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT
                << '.'
                << SNAP_LOG_SEND;

            ed::message lock_failed_message;
            lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
            lock_failed_message.reply_to(msg);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, entering_key);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
            f_messenger->send_message(lock_failed_message);

            return;
        }
    }

    if(!is_daemon_ready())
    {
        SNAP_LOG_TRACE
            << "caching LOCK message for \""
            << object_name
            << "\" ("
            << tag
            << ") as the cluck system is not yet considered ready."
            << SNAP_LOG_SEND;

        f_message_cache.emplace_back(timeout, msg);

        // make sure the cache gets cleaned up if the message times out
        //
        std::int64_t const timeout_date(f_timer->get_timeout_date());
        if(timeout_date == -1
        || cluck::timeout_t(timeout_date / 1'000'000, timeout_date % 1'000'000) > timeout)
        {
            f_timer->set_timeout_date(timeout);
        }
        return;
    }

    if(is_leader() == nullptr)
    {
        // we are not a leader, we need to forward the message to one
        // of the leaders instead
        //
        forward_message_to_leader(msg);
        return;
    }

    // make sure this is a new ticket
    //
    auto entering_ticket(f_entering_tickets.find(object_name));
    if(entering_ticket != f_entering_tickets.end())
    {
        auto key_ticket(entering_ticket->second.find(entering_key));
        if(key_ticket != entering_ticket->second.end())
        {
            // if this is a re-LOCK, then it may be a legitimate duplicate
            // in which case we do not want to generate a LOCK_FAILED error
            //
            if(msg.has_parameter(cluck::g_name_cluck_param_serial))
            {
                ticket::serial_t const serial(msg.get_integer_parameter(cluck::g_name_cluck_param_serial));
                if(key_ticket->second->get_serial() == serial)
                {
                    // legitimate double request from leaders
                    // (this happens when a leader dies and we have to restart
                    // a lock negotiation)
                    //
                    return;
                }
            }

            // the object already exists... do not allow duplicates
            //
            SNAP_LOG_ERROR
                << "an entering ticket has the same object name \""
                << object_name
                << "\" ("
                << tag
                << ") and entering key \""
                << entering_key
                << "\"."
                << SNAP_LOG_SEND;

            ed::message lock_failed_message;
            lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
            lock_failed_message.reply_to(msg);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, entering_key);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_duplicate);
            f_messenger->send_message(lock_failed_message);

            return;
        }
    }

    // make sure there is not a ticket with the same name already defined
    //
    // (this is is really important so we can actually properly UNLOCK an
    // existing lock since we use the same search and if two entries were
    // to be the same we could not know which to unlock; there are a few
    // other places where such a search is used actually...)
    //
    auto obj_ticket(f_tickets.find(object_name));
    if(obj_ticket != f_tickets.end())
    {
        auto key_ticket(std::find_if(
                  obj_ticket->second.begin()
                , obj_ticket->second.end()
                , [&entering_key](auto const & t)
                {
                    return t.second->get_entering_key() == entering_key;
                }));
        if(key_ticket != obj_ticket->second.end())
        {
            // there is already a ticket with this object name/entering key
            //
            SNAP_LOG_ERROR
                << "a ticket has the same object name \""
                << object_name
                << "\" ("
                << tag
                << ") and entering key \""
                << entering_key
                << "\"."
                << SNAP_LOG_SEND;

            ed::message lock_failed_message;
            lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
            lock_failed_message.reply_to(msg);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, tag);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, entering_key);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_duplicate);
            f_messenger->send_message(lock_failed_message);

            return;
        }
    }

    ticket::pointer_t ticket(std::make_shared<ticket>(
                                  this
                                , f_messenger
                                , object_name
                                , tag
                                , entering_key
                                , timeout
                                , duration
                                , server_name
                                , service_name));

    f_entering_tickets[object_name][entering_key] = ticket;

    // finish up ticket initialization
    //
    ticket->set_unlock_duration(unlock_duration);

    // generate a serial number for that ticket
    //
    f_ticket_serial = (f_ticket_serial + 1) & 0x00FFFFFF;
    if(f_leaders[0]->get_id() != f_my_id)
    {
        if(f_leaders.size() >= 2
        && f_leaders[1]->get_id() != f_my_id)
        {
            f_ticket_serial |= 1 << 24;
        }
        else if(f_leaders.size() >= 3
             && f_leaders[2]->get_id() != f_my_id)
        {
            f_ticket_serial |= 2 << 24;
        }
    }
    ticket->set_serial(f_ticket_serial);

    if(msg.has_parameter(cluck::g_name_cluck_param_serial))
    {
        // if we have a "serial" number in that message, we lost a leader
        // and when that happens we are not unlikely to have lost the
        // client that requested the LOCK, send an ALIVE message to make
        // sure that the client still exists before entering the ticket
        //
        ticket->set_alive_timeout(snapdev::now() + cluck::timeout_t(5, 0));

        ed::message alive_message;
        alive_message.set_command(ed::g_name_ed_cmd_alive);
        alive_message.set_server(server_name);
        alive_message.set_service(service_name);
        alive_message.add_parameter(ed::g_name_ed_param_serial, "relock/" + object_name + '/' + entering_key);
        alive_message.add_parameter(ed::g_name_ed_param_timestamp, time(nullptr));
        f_messenger->send_message(alive_message);
    }
    else
    {
        // act on the new ticket
        //
        ticket->entering();
    }

    // the list of tickets changed, make sure we update the timeout timer
    //
    cleanup();
}


/** \brief Acknowledgement of the lock to activate.
 *
 * This function is an acknowledgement that the lock can now be
 * activated. This is true only if the 'key' and 'other_key'
 * are a match, though.
 *
 * \param[in] msg  The LOCK_ACTIVATED message.
 */
void cluckd::msg_lock_activated(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    std::string const & other_key(msg.get_parameter(cluck::g_name_cluck_param_other_key));
    if(other_key == key)
    {
        auto obj_ticket(f_tickets.find(object_name));
        if(obj_ticket != f_tickets.end())
        {
            auto key_ticket(obj_ticket->second.find(key));
            if(key_ticket != obj_ticket->second.end())
            {
                // that key is still here!
                // time to activate
                //
                key_ticket->second->lock_activated();
            }
        }
    }
}


/** \brief Tell all the tickets that we received a LOCK_ENTERED message.
 *
 * This function calls all the tickets entered() function which
 * processes the LOCK_ENTERED message.
 *
 * We pass the key and "our ticket" number along so it can actually
 * create the ticket if required.
 *
 * \param[in] msg  The LOCK_ENTERED message.
 */
void cluckd::msg_lock_entered(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    auto const obj_entering_ticket(f_entering_tickets.find(object_name));
    if(obj_entering_ticket != f_entering_tickets.end())
    {
        auto const key_entering_ticket(obj_entering_ticket->second.find(key));
        if(key_entering_ticket != obj_entering_ticket->second.end())
        {
            key_entering_ticket->second->entered();
        }
    }
}


/** \brief Remove a ticket we are done with (i.e. unlocked).
 *
 * This command drops the specified ticket (object_name).
 *
 * \param[in] msg  The entering message.
 */
void cluckd::msg_lock_entering(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    cluck::timeout_t timeout;
    std::string key;
    std::string source;
    if(!get_parameters(msg, &object_name, &tag, nullptr, &timeout, &key, &source))
    {
        return;
    }

    // the server_name and client_pid never include a slash so using
    // such as separators is safe
    //
    if(timeout > snapdev::now())  // lock still in the future?
    {
        if(is_daemon_ready())              // still have leaders?
        {
            // the entering is just a flag (i.e. entering[i] = true)
            // in our case the existance of a ticket is enough to know
            // that we entered
            //
            bool allocate(true);
            auto const obj_ticket(f_entering_tickets.find(object_name));
            if(obj_ticket != f_entering_tickets.end())
            {
                auto const key_ticket(obj_ticket->second.find(key));
                allocate = key_ticket == obj_ticket->second.end();
            }
            if(allocate)
            {
                // ticket does not exist, so create it now
                // (note: ticket should only exist on originator)
                //
                cluck::timeout_t const duration(msg.get_timespec_parameter(cluck::g_name_cluck_param_duration));
                if(duration < cluck::CLUCK_MINIMUM_TIMEOUT)
                {
                    // invalid duration
                    //
                    SNAP_LOG_ERROR
                        << duration
                        << " is an invalid duration, the minimum accepted is "
                        << cluck::CLUCK_MINIMUM_TIMEOUT
                        << "."
                        << SNAP_LOG_SEND;

                    ed::message lock_failed_message;
                    lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
                    lock_failed_message.reply_to(msg);
                    lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
                    lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, key);
                    lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
                    f_messenger->send_message(lock_failed_message);

                    return;
                }

                cluck::timeout_t unlock_duration(cluck::CLUCK_DEFAULT_TIMEOUT);
                if(msg.has_parameter(cluck::g_name_cluck_param_unlock_duration))
                {
                    unlock_duration = msg.get_timespec_parameter(cluck::g_name_cluck_param_unlock_duration);
                    if(unlock_duration != cluck::CLUCK_DEFAULT_TIMEOUT
                    && unlock_duration < cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT)
                    {
                        // invalid duration, minimum is 60
                        //
                        SNAP_LOG_ERROR
                            << duration
                            << " is an invalid unlock duration, the minimum accepted is "
                            << cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT
                            << "."
                            << SNAP_LOG_SEND;

                        ed::message lock_failed_message;
                        lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
                        lock_failed_message.reply_to(msg);
                        lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
                        lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, key);
                        lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
                        f_messenger->send_message(lock_failed_message);

                        return;
                    }
                }

                // we have to know where this message comes from
                //
                std::vector<std::string> source_segments;
                if(snapdev::tokenize_string(source_segments, source, "/") != 2)
                {
                    SNAP_LOG_ERROR
                        << "Invalid number of parameters in source parameter (found "
                        << source_segments.size()
                        << ", expected 2)."
                        << SNAP_LOG_SEND;

                    ed::message lock_failed_message;
                    lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
                    lock_failed_message.reply_to(msg);
                    lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
                    lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, key);
                    lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_invalid);
                    f_messenger->send_message(lock_failed_message);

                    return;
                }

                ticket::pointer_t ticket(std::make_shared<ticket>(
                                          this
                                        , f_messenger
                                        , object_name
                                        , tag
                                        , key
                                        , timeout
                                        , duration
                                        , source_segments[0]
                                        , source_segments[1]));

                f_entering_tickets[object_name][key] = ticket;

                // finish up on ticket initialization
                //
                ticket->set_owner(msg.get_sent_from_server());
                ticket->set_unlock_duration(unlock_duration);
                ticket->set_serial(msg.get_integer_parameter(cluck::g_name_cluck_param_serial));
            }

            ed::message reply;
            reply.set_command(cluck::g_name_cluck_cmd_lock_entered);
            reply.reply_to(msg);
            reply.add_parameter(cluck::g_name_cluck_param_object_name, object_name);
            reply.add_parameter(cluck::g_name_cluck_param_tag, tag);
            reply.add_parameter(cluck::g_name_cluck_param_key, key);
            f_messenger->send_message(reply);
        }
        else
        {
            SNAP_LOG_DEBUG
                << "received LOCK_ENTERING while we are thinking we are not ready."
                << SNAP_LOG_SEND;
        }
    }

    cleanup();
}


/** \brief Remove a ticket we are done with (i.e. unlocked).
 *
 * This command drops the specified ticket (object_name).
 *
 * \param[in] msg  The exiting message.
 */
void cluckd::msg_lock_exiting(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    // when exiting we just remove the entry with that key
    //
    auto const obj_entering(f_entering_tickets.find(object_name));
    if(obj_entering != f_entering_tickets.end())
    {
        auto const key_entering(obj_entering->second.find(key));
        if(key_entering != obj_entering->second.end())
        {
            obj_entering->second.erase(key_entering);

            // we also want to remove it from the ticket f_entering
            // map if it is there (older ones are there!)
            //
            bool run_activation(false);
            auto const obj_ticket(f_tickets.find(object_name));
            if(obj_ticket != f_tickets.end())
            {
                for(auto const & key_ticket : obj_ticket->second)
                {
                    key_ticket.second->remove_entering(key);
                    run_activation = true;
                }
            }
            if(run_activation)
            {
                // try to activate the lock right now since it could
                // very well be the only ticket and that is exactly
                // when it is viewed as active!
                //
                // Note: this is from my old version, if I am correct
                //       it cannot happen anymore because (1) this is
                //       not the owner so the activation would not
                //       take anyway and (2) the ticket is not going
                //       to be marked as being ready at this point
                //       (that happens later)
                //
                //       XXX we probably should remove this statement
                //           and the run_activation flag which would
                //           then be useless
                //
                activate_first_lock(object_name);
            }

            if(obj_entering->second.empty())
            {
                f_entering_tickets.erase(obj_entering);
            }
        }
    }

    // the list of tickets is not unlikely changed so we need to make
    // a call to cleanup to make sure the timer is reset appropriately
    //
    cleanup();
}


/** \brief Acknowledgement a lock failure.
 *
 * This function handles the LOCK_FAILED event that another leader may send
 * to us. In that case we have to stop the process.
 *
 * LOCK_FAILED can happen mainly because of tainted data so we should never
 * get here within a leader. However, with time we may add a few errors
 * which could happen for other reasons than just tainted data.
 *
 * When this function finds an entering ticket or a plain ticket to remove
 * according to the object name and key found in the LOCK_FAILED message,
 * it forwards the LOCK_FAILED message to the server and service found in
 * the ticket.
 *
 * \todo
 * This function destroys a ticket even if it is already considered locked.
 * Make double sure that this is okay with a LOCK_FAILED sent to the client.
 *
 * \warning
 * Although this event should not occur, it is problematic since anyone
 * can send a LOCK_FAILED message here and as a side effect destroy a
 * perfectly valid ticket.
 *
 * \param[in] msg  The LOCK_FAILED message.
 */
void cluckd::msg_lock_failed(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    std::string forward_server;
    std::string forward_service;

    // remove f_entering_tickets entries if we find matches there
    //
    auto obj_entering(f_entering_tickets.find(object_name));
    if(obj_entering != f_entering_tickets.end())
    {
        auto key_entering(obj_entering->second.find(key));
        if(key_entering != obj_entering->second.end())
        {
            forward_server = key_entering->second->get_server_name();
            forward_service = key_entering->second->get_service_name();

            obj_entering->second.erase(key_entering);
        }

        if(obj_entering->second.empty())
        {
            obj_entering = f_entering_tickets.erase(obj_entering);
        }
        else
        {
            ++obj_entering;
        }
    }

    // remove any f_tickets entries if we find matches there
    //
    auto obj_ticket(f_tickets.find(object_name));
    if(obj_ticket != f_tickets.end())
    {
        bool try_activate(false);
        auto key_ticket(obj_ticket->second.find(key));
        if(key_ticket == obj_ticket->second.end())
        {
            key_ticket = std::find_if(
                      obj_ticket->second.begin()
                    , obj_ticket->second.end()
                    , [&key](auto const & t)
                    {
                        return t.second->get_entering_key() == key;
                    });
        }
        if(key_ticket != obj_ticket->second.end())
        {
            // Note: if we already found it in the f_entering_tickets then
            //       the server and service names are going to be exactly
            //       the same so there is no need to test that here
            //
            forward_server = key_ticket->second->get_server_name();
            forward_service = key_ticket->second->get_service_name();

            obj_ticket->second.erase(key_ticket);
            try_activate = true;
        }

        if(obj_ticket->second.empty())
        {
            obj_ticket = f_tickets.erase(obj_ticket);
        }
        else
        {
            if(try_activate)
            {
                // something was erased, a new ticket may be first
                //
                activate_first_lock(obj_ticket->first);
            }

            ++obj_ticket;
        }
    }

    if(!forward_server.empty()
    && !forward_service.empty())
    {
        // we deleted an entry, forward the message to the service
        // that requested that lock
        //
        msg.set_server(forward_server);
        msg.set_service(forward_service);
        f_messenger->send_message(msg);
    }

    // the list of tickets is not unlikely changed so we need to make
    // a call to cleanup to make sure the timer is reset appropriately
    //
    cleanup();
}


/** \brief The list of leaders.
 *
 * This function receives the list of leaders after an election.
 *
 * \param[in] msg  The LOCK_LEADERS message.
 */
void cluckd::msg_lock_leaders(ed::message & msg)
{
    f_election_date = msg.get_timespec_parameter(cluck::g_name_cluck_param_election_date);

    // save the new leaders in our own list
    //
    f_leaders.clear();
    for(int idx(0); idx < 3; ++idx)
    {
        std::string const param_name(cluck::g_name_cluck_param_leader + std::to_string(idx));
        if(msg.has_parameter(param_name))
        {
            computer::pointer_t leader(std::make_shared<computer>());
            std::string const lockid(msg.get_parameter(param_name));
            if(leader->set_id(lockid))
            {
                computer::map_t::iterator exists(f_computers.find(leader->get_name()));
                if(exists != f_computers.end())
                {
                    // it already exists, use our existing instance
                    //
                    f_leaders.push_back(exists->second);
                }
                else
                {
                    // we do not yet know of that computer, even though
                    // it is a leader! (i.e. we are not yet aware that
                    // somehow we are connected to it)
                    //
                    leader->set_connected(false);
                    f_computers[leader->get_name()] = leader;

                    f_leaders.push_back(leader);
                }
            }
        }
    }

    if(!f_leaders.empty())
    {
        synchronize_leaders();

        // set the round-robin position to a random value
        //
        // note: I know the result is likely skewed, c will be set to
        // a number between 0 and 255 and modulo 3 means that you get
        // one extra zero (255 % 3 == 0); however, there are 85 times
        // 3 in 255 so it probably won't be noticeable.
        //
        std::uint8_t c;
        RAND_bytes(reinterpret_cast<unsigned char *>(&c), sizeof(c));
        f_next_leader = c % f_leaders.size();
    }

    // the is_daemon_ready() function depends on having f_leaders defined
    // and when that happens we may need to empty our cache
    //
    check_lock_status();
}


/** \brief Called whenever a cluck computer is acknowledging itself.
 *
 * This function gets called on a LOCK_STARTED event which is sent whenever
 * a cluck process is initialized on a computer.
 *
 * The message is expected to include the computer name. At this time
 * we cannot handle having more than one instance one the same computer.
 *
 * \param[in] message  The LOCK_STARTED message.
 */
void cluckd::msg_lock_started(ed::message & msg)
{
    // I do not think we would ever message ourselves, but in case it happens
    // the rest of the function does not support that case
    //
    std::string const server_name(msg.get_parameter(communicatord::g_name_communicatord_param_server_name));
    if(server_name == f_server_name)
    {
        return;
    }

    cluck::timeout_t const start_time(msg.get_timespec_parameter(cluck::g_name_cluck_param_start_time));

    computer::map_t::iterator it(f_computers.find(server_name));
    bool new_computer(it == f_computers.end());
    if(new_computer)
    {
        // create a computer instance so we know it exists
        //
        computer::pointer_t computer(std::make_shared<computer>());

        // fill the fields from the "lock_id" parameter
        //
        if(!computer->set_id(msg.get_parameter(cluck::g_name_cluck_param_lock_id)))
        {
            // this is not a valid identifier, ignore altogether
            //
            return;
        }
        computer->set_start_time(start_time);

        f_computers[computer->get_name()] = computer;
    }
    else
    {
        if(!it->second->get_connected())
        {
            // we heard of this computer (because it is/was a leader) but
            // we had not yet received a LOCK_STARTED message from it; so here
            // we consider it a new computer and will reply to the LOCK_STARTED
            //
            new_computer = true;
            it->second->set_connected(true);
        }

        if(it->second->get_start_time() != start_time)
        {
            // when the start time changes that means cluckd
            // restarted which can happen without communicatord
            // restarting so here we would not know about the feat
            // without this parameter and in this case it is very
            // much the same as a new computer so send it a
            // LOCK_STARTED message back
            //
            new_computer = true;
            it->second->set_start_time(start_time);
        }
    }

    // keep the newest election results
    //
    if(msg.has_parameter(cluck::g_name_cluck_param_election_date))
    {
        snapdev::timespec_ex const election_date(msg.get_timespec_parameter(cluck::g_name_cluck_param_election_date));
        if(election_date > f_election_date)
        {
            f_election_date = election_date;
            f_leaders.clear();
        }
    }

    bool const set_my_leaders(f_leaders.empty());
    if(set_my_leaders)
    {
        for(int idx(0); idx < 3; ++idx)
        {
            std::string const param_name(cluck::g_name_cluck_param_leader + std::to_string(idx));
            if(msg.has_parameter(param_name))
            {
                computer::pointer_t leader(std::make_shared<computer>());
                std::string const lockid(msg.get_parameter(param_name));
                if(leader->set_id(lockid))
                {
                    computer::map_t::iterator exists(f_computers.find(leader->get_name()));
                    if(exists != f_computers.end())
                    {
                        // it already exists, use our existing instance
                        //
                        f_leaders.push_back(exists->second);
                    }
                    else
                    {
                        // we do not yet know of that computer, even though
                        // it is a leader! (i.e. we are not yet aware that
                        // somehow we are connected to it)
                        //
                        leader->set_connected(false);
                        f_computers[leader->get_name()] = leader;

                        f_leaders.push_back(leader);
                    }
                }
            }
        }
    }

    election_status();

    // this can have an effect on the lock statuses
    //
    check_lock_status();

    if(new_computer)
    {
        // send a reply if that was a new computer
        //
        send_lock_started(&msg);
    }
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
void cluckd::msg_lock_status(ed::message & msg)
{
    ed::message status_message;
    status_message.set_command(is_daemon_ready()
            ? cluck::g_name_cluck_cmd_lock_ready
            : cluck::g_name_cluck_cmd_no_lock);
SNAP_LOG_WARNING << "sending lock status (reply to LOCK_STATUS): " << status_message.get_command() << SNAP_LOG_SEND;
    status_message.reply_to(msg);
    status_message.add_parameter(communicatord::g_name_communicatord_param_cache, communicatord::g_name_communicatord_value_no);
    f_messenger->send_message(status_message);
}


/** \brief Another cluckd is sending us its list of tickets.
 *
 * Whenever a cluckd dies, a new one is quickly promoted as a leader
 * and that new leader would have no idea about the existing tickets
 * (locks) so the other two send it a LOCK_TICKETS message.
 *
 * The tickets are defined in the parameter of the same name using
 * the serialization function to transform the objects in a string.
 * Here we can unserialize that string accordingly.
 *
 * First we extract the object name and entering key to see whether
 * we have that ticket already defined. If so, then we unserialize
 * in that existing object. The extraction is additive so we can do
 * it any number of times.
 *
 * \param[in] msg  The message to reply to.
 */
void cluckd::msg_lock_tickets(ed::message & msg)
{
    std::string const tickets(msg.get_parameter(cluck::g_name_cluck_param_tickets));

    // we have one ticket per line, so we first split per line and then
    // work on lines one at a time
    //
    std::list<std::string> lines;
    snapdev::tokenize_string(lines, tickets, "\n", true);
    for(auto const & l : lines)
    {
        ticket::pointer_t t;
        std::list<std::string> vars;
        snapdev::tokenize_string(vars, tickets, "|", true);
        auto object_name_value(std::find_if(
                  vars.begin()
                , vars.end()
                , [](std::string const & vv)
                {
                    return vv.starts_with("object_name=");
                }));
        if(object_name_value != vars.end())
        {
            auto entering_key_value(std::find_if(
                      vars.begin()
                    , vars.end()
                    , [](std::string const & vv)
                    {
                        return vv.starts_with("entering_key=");
                    }));
            if(entering_key_value != vars.end())
            {
                // extract the values which start after the '=' sign
                //
                std::string const object_name(object_name_value->substr(12));
                std::string const entering_key(entering_key_value->substr(13));

                auto entering_ticket(f_entering_tickets.find(object_name));
                if(entering_ticket != f_entering_tickets.end())
                {
                    auto key_ticket(entering_ticket->second.find(entering_key));
                    if(key_ticket != entering_ticket->second.end())
                    {
                        t = key_ticket->second;
                    }
                }
                if(t == nullptr)
                {
                    auto obj_ticket(f_tickets.find(object_name));
                    if(obj_ticket != f_tickets.end())
                    {
                        auto key_ticket(std::find_if(
                                  obj_ticket->second.begin()
                                , obj_ticket->second.end()
                                , [&entering_key](auto const & o)
                                {
                                    return o.second->get_entering_key() == entering_key;
                                }));
                        if(key_ticket != obj_ticket->second.end())
                        {
                            t = key_ticket->second;
                        }
                    }
                }

                // ticket exists? if not create a new one
                //
                bool const new_ticket(t == nullptr);
                if(new_ticket)
                {
                    // create a new ticket, some of the parameters are there just
                    // because they are required; they will be replaced by the
                    // unserialize call below...
                    //
                    t = std::make_shared<ticket>(
                                  this
                                , f_messenger
                                , object_name
                                , ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG
                                , entering_key
                                , cluck::CLUCK_DEFAULT_TIMEOUT + snapdev::now()
                                , cluck::CLUCK_DEFAULT_TIMEOUT
                                , f_server_name
                                , cluck::g_name_cluck_service_name);
                }

                t->unserialize(l);

                // do a couple of additional sanity tests to
                // make sure that we want to keep new tickets
                //
                // first make sure it is marked as "locked"
                //
                // second check that the owner is a leader that
                // exists (the sender uses a LOCK message for
                // locks that are not yet locked or require
                // a new owner)
                //
                if(new_ticket
                && t->is_locked())
                {
                    auto li(std::find_if(
                              f_leaders.begin()
                            , f_leaders.end()
                            , [&t](auto const & c)
                            {
                                return t->get_owner() == c->get_name();
                            }));
                    if(li != f_leaders.end())
                    {
                        f_tickets[object_name][t->get_ticket_key()] = t;
                    }
                }
            }
        }
    }
}


/** \brief Search for the largest ticket.
 *
 * This function searches the list of tickets for the largest one
 * and records that number.
 *
 * If a quorum is reached when adding this ticket, then an ADD_TICKET reply
 * is sent back to the sender.
 *
 * \param[in] msg  The MAX_TICKET message being handled.
 */
void cluckd::msg_max_ticket(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    // the MAX_TICKET is an answer that has to go in a still un-added ticket
    //
    auto const obj_entering_ticket(f_entering_tickets.find(object_name));
    if(obj_entering_ticket != f_entering_tickets.end())
    {
        auto const key_entering_ticket(obj_entering_ticket->second.find(key));
        if(key_entering_ticket != obj_entering_ticket->second.end())
        {
            key_entering_ticket->second->max_ticket(msg.get_integer_parameter(cluck::g_name_cluck_param_ticket_id));
        }
    }
}


/** \brief Called whenever a remote connection is disconnected.
 *
 * This function is used to know that a remote connection was
 * disconnected.
 *
 * This function handles the HANGUP, DISCONNECTED, and STATUS(down)
 * nessages as required.
 *
 * This allows us to manage the f_computers list of computers running
 * cluckd services.
 *
 * \param[in] msg  The DISCONNECTED or HANGUP or STATUS message.
 */
void cluckd::msg_server_gone(ed::message & msg)
{
    // if the server is not defined, ignore that message
    //
    if(!msg.has_parameter(communicatord::g_name_communicatord_param_server_name))
    {
        return;
    }

    // was it a cluckd service at least?
    //
    std::string const server_name(msg.get_parameter(communicatord::g_name_communicatord_param_server_name));
    if(server_name.empty()
    || server_name == f_server_name)
    {
        // we never want to remove ourselves?!
        //
        return;
    }

    // is "server_name" known?
    //
    auto it(f_computers.find(server_name));
    if(it == f_computers.end())
    {
        // no computer found, nothing else to do here
        //
        return;
    }
    computer::pointer_t c(it->second);

    // got it, remove it
    //
    f_computers.erase(it);

    // is that computer a leader?
    //
    auto li(std::find(
              f_leaders.begin()
            , f_leaders.end()
            , c));
    if(li != f_leaders.end())
    {
        f_leaders.erase(li);

        // elect another computer in case the one we just erased was a leader
        //
        // (of course, no elections occur unless we are the computer with the
        // smallest IP address)
        //
        election_status();

        // if too many leaders were dropped, we may go back to the NO_LOCK status
        //
        // we only send a NO_LOCK if the election could not re-assign another
        // computer to replace the missing leader(s)
        //
        check_lock_status();
    }
}


/** \brief With the STATUS message we know of new communicatord services.
 *
 * This function captures the STATUS message and if it sees that the
 * name of the service is a remote communicator daemon then it
 * sends a new LOCK_STARTED message to make sure that all cluck daemons
 * are aware of us.
 *
 * \param[in] msg  The STATUS message.
 */
void cluckd::msg_status(ed::message & msg)
{
    // check the service name, it has to be one that means it is a remote
    // connection with another communicator daemon
    //
    std::string const service(msg.get_parameter(communicatord::g_name_communicatord_param_service));

    if(service.starts_with(communicatord::g_name_communicatord_connection_remote_communicator_in)   // remote host connected to us
    || service.starts_with(communicatord::g_name_communicatord_connection_remote_communicator_out)) // we connected to remote host
    {
        // check what the status is now: "up" or "down"
        //
        std::string const status(msg.get_parameter(communicatord::g_name_communicatord_param_status));
        if(status == communicatord::g_name_communicatord_value_up)
        {
            // we already broadcast a LOCK_STARTED from CLUSTER_UP
            // and that's enough
            //
            ;
        }
        else
        {
            // host is down, remove from our list of hosts
            //
            msg_server_gone(msg);
        }
    }
}


/** \brief Acknowledgement that the ticket was properly added.
 *
 * This function gets called whenever the ticket was added on another
 * leader.
 *
 * \param[in] msg  The TICKET_ADDED message being handled.
 */
void cluckd::msg_ticket_added(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    auto const obj_ticket(f_tickets.find(object_name));
    if(obj_ticket != f_tickets.end())
    {
        auto const key_ticket(obj_ticket->second.find(key));
        if(key_ticket != obj_ticket->second.end())
        {
            // this ticket exists on this system
            //
            auto const obj_entering_ticket(f_entering_tickets.find(object_name));
            if(obj_entering_ticket == f_entering_tickets.end())
            {
                // this happens all the time because the entering ticket
                // gets removed on the first TICKET_ADDED we receive so
                // on the second one we get here...
                //
                SNAP_LOG_TRACE
                    << "called with object \""
                    << object_name
                    << "\" not present in f_entering_ticket (key: \""
                    << key
                    << "\")."
                    << SNAP_LOG_SEND;
                return;
            }
            key_ticket->second->ticket_added(obj_entering_ticket->second);
        }
        else
        {
            SNAP_LOG_WARNING
                << "found object \""
                << object_name
                << "\" but could not find a corresponding ticket with key \""
                << key
                << "\"..."
                << SNAP_LOG_SEND;
        }
    }
    else
    {
        SNAP_LOG_WARNING
            << "object \""
            << object_name
            << "\" not found."
            << SNAP_LOG_SEND;
    }
}


/** \brief Let other leaders know that the ticket is ready.
 *
 * This message is received when the owner of a ticket marks a
 * ticket as ready. This means the ticket is available for locking.
 *
 * \param[in] msg  The TICKET_READY message.
 */
void cluckd::msg_ticket_ready(ed::message & msg)
{
    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    std::string key;
    if(!get_parameters(msg, &object_name, &tag, nullptr, nullptr, &key, nullptr))
    {
        return;
    }

    auto obj_ticket(f_tickets.find(object_name));
    if(obj_ticket != f_tickets.end())
    {
        auto key_ticket(obj_ticket->second.find(key));
        if(key_ticket != obj_ticket->second.end())
        {
            // we can mark this ticket as activated
            //
            key_ticket->second->set_ready();
        }
    }
}


/** \brief Unlock the resource.
 *
 * This function unlocks the resource specified in the call to lock().
 *
 * \param[in] msg  The unlock message.
 *
 * \sa msg_lock()
 */
void cluckd::msg_unlock(ed::message & msg)
{
    if(!is_daemon_ready())
    {
        SNAP_LOG_ERROR
            << "received an UNLOCK when cluckd is not ready to receive lock related messages."
            << SNAP_LOG_SEND;
        return;
    }

    if(is_leader() == nullptr)
    {
        // we are not a leader, we need to forward to a leader to handle
        // the message properly
        //
        forward_message_to_leader(msg);
        return;
    }

    std::string object_name;
    ed::dispatcher_match::tag_t tag(ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG);
    pid_t client_pid(0);
    if(!get_parameters(msg, &object_name, &tag, &client_pid, nullptr, nullptr, nullptr))
    {
        return;
    }

    // if the ticket still exists, send the UNLOCKED and then erase it
    //
    auto obj_ticket(f_tickets.find(object_name));
    if(obj_ticket != f_tickets.end())
    {
        std::string const server_name(msg.has_parameter("lock_proxy_server_name")
                                    ? msg.get_parameter("lock_proxy_server_name")
                                    : msg.get_sent_from_server());

        //std::string const service_name(msg.has_parameter("lock_proxy_service_name")
        //                            ? msg.get_parameter("lock_proxy_service_name")
        //                            : msg.get_sent_from_service());

        std::string const entering_key(server_name + '/' + std::to_string(client_pid));
        auto key_ticket(std::find_if(
                  obj_ticket->second.begin()
                , obj_ticket->second.end()
                , [&entering_key](auto const & t)
                {
                    return t.second->get_entering_key() == entering_key;
                }));
        if(key_ticket != obj_ticket->second.end())
        {
            // this function will send a DROPTICKET to the other leaders
            // and the UNLOCKED to the source (unless we already sent the
            // UNLOCKED which gets sent at most once.)
            //
            key_ticket->second->drop_ticket();

            obj_ticket->second.erase(key_ticket);
            if(obj_ticket->second.empty())
            {
                // we are done with this one!
                //
                f_tickets.erase(obj_ticket);
            }
        }
else SNAP_LOG_WARNING << "and we could not find key \"" << entering_key << "\" in that object's map..." << SNAP_LOG_SEND;
    }

    // reset the timeout with the other locks
    //
    cleanup();
}



} // namespace cluck_daemon
// vim: ts=4 sw=4 et
