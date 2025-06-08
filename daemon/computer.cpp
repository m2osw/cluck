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
#include    "computer.h"


// cluck
//
#include    <cluck/exception.h>


// cppthread
//
#include    <cppthread/thread.h>


// snaplogger
//
#include    <snaplogger/message.h>


// libaddr
//
#include    <libaddr/addr_parser.h>
#include    <libaddr/exception.h>


// advgetopt
//
#include    <advgetopt/validator_integer.h>


// snapdev
//
#include    <snapdev/tokenize_string.h>


// C++
//
#include    <iomanip>


// openssl
//
#include    <openssl/rand.h>


// last include
//
#include    <snapdev/poison.h>



namespace cluck_daemon
{



computer::computer()
{
    // used for a remote computer, we'll eventually get a set_id() which
    // defines the necessary computer parameters
}


computer::computer(std::string const & name, priority_t priority, addr::addr ip_address)
    : f_self(true)
    , f_priority(priority)
    , f_ip_address(ip_address)
    , f_pid(getpid())
    , f_name(name)
{
    // verify the name becaue it can cause issues in the serializer if it
    // includes invalid characters are is empty
    //
    if(f_name.empty())
    {
        throw cluck::invalid_parameter("the computer name cannot be an empty string.");
    }
    std::string invalid_name_characters("|");
    invalid_name_characters += '\0';
    if(f_name.find_first_of(invalid_name_characters) != std::string::npos)
    {
        throw cluck::invalid_parameter("a computer name cannot include the '|' or null characters.");
    }

    if(f_priority < PRIORITY_USER_MIN
    || f_priority > PRIORITY_MAX)
    {
        throw cluck::invalid_parameter(
              "priority is limited to a number between "
            + std::to_string(static_cast<int>(PRIORITY_USER_MIN))
            + " and "
            + std::to_string(static_cast<int>(PRIORITY_MAX))
            + " inclusive.");
    }

    RAND_bytes(reinterpret_cast<unsigned char *>(&f_random_id), sizeof(f_random_id));
}


bool computer::is_self() const
{
    return f_self;
}


void computer::set_connected(bool connected)
{
    f_connected = connected;
}


bool computer::get_connected() const
{
    return f_connected;
}


/** \brief Initialize this computer object from \p id.
 *
 * \note
 * At the moment, the function is expected to work each time. This is
 * used internally to the cluckd, so the errors are due primarily to
 * a spurious message from a hacker. Otherwise, we would have to keep
 * all the new parameter on the stack and save them in the object
 * fields only if all are considered valid. (i.e. If the input string
 * is invalid, the computer object is likely left in an invalid state.)
 *
 * \exception logic_error
 * If the function was already called once successfully, then this
 * exception is raised when trying to do call it again.
 *
 * \return true if the id was considered 100% valid, false otherwise.
 */
bool computer::set_id(std::string const & id)
{
    if(f_priority != PRIORITY_UNDEFINED)
    {
        throw cluck::logic_error("computer::set_id() cannot be called more than once.");
    }

    std::vector<std::string> parts;
    snapdev::tokenize_string(parts, id, "|");
    if(parts.size() != 5)
    {
        // do not throw in case something changes we do not want snaplock to
        // "crash" over and over again
        //
        SNAP_LOG_ERROR
            << "received a computer id which does not have exactly 5 parts: \""
            << id
            << "\"."
            << SNAP_LOG_SEND;
        return false;
    }

    // base is VERY IMPORTANT for this one as we save priorities below ten
    // as 0n (01 to 09) so the sort works as expected
    //
    std::int64_t value(0);
    bool valid(advgetopt::validator_integer::convert_string(parts[0], value));
    if(!valid
    || value < PRIORITY_USER_MIN
    || value > PRIORITY_MAX)
    {
        SNAP_LOG_ERROR
            << "priority is limited to a number between "
            << static_cast<int>(PRIORITY_USER_MIN)
            << " and "
            << static_cast<int>(PRIORITY_MAX)
            << " inclusive."
            << SNAP_LOG_SEND;
        return false;
    }
    f_priority = value;

    if(!advgetopt::validator_integer::convert_string(parts[1], value))
    {
        SNAP_LOG_ERROR
            << "random value is expected to be a valid integer, not "
            << parts[1]
            << "."
            << SNAP_LOG_SEND;
        return false;
    }
    f_random_id = value;

    if(parts[2].empty())
    {
        SNAP_LOG_ERROR
            << "the process IP cannot be an empty string."
            << SNAP_LOG_SEND;
        return false;
    }
    try
    {
        f_ip_address = addr::string_to_addr(parts[2]);
        if(f_ip_address.is_default())
        {
            SNAP_LOG_ERROR
                << "the IP address cannot be the default IP (0.0.0.0)."
                << SNAP_LOG_SEND;
            return false;
        }
    }
    catch(addr::addr_invalid_argument const & e)
    {
        // we want to avoid "crashing" because any hacker could send us an
        // invalid message and get the service to stop on this one
        //
        SNAP_LOG_ERROR
            << "the process IP, \""
            << parts[2]
            << "\", is not valid: "
            << e.what()
            << SNAP_LOG_SEND;
        return false;
    }

    valid = advgetopt::validator_integer::convert_string(parts[3], value);
    if(!valid
    || value < 1
    || value > cppthread::get_pid_max())
    {
        SNAP_LOG_ERROR
            << "process identifier "
            << parts[3]
            << " is invalid ("
            << std::boolalpha << valid
            << ") or out of bounds: [1.."
            << cppthread::get_pid_max()
            << "]."
            << SNAP_LOG_SEND;
        return false;
    }
    f_pid = value;

    if(parts[4].empty())
    {
        SNAP_LOG_ERROR
            << "the server name in the lock identifier cannot be empty."
            << SNAP_LOG_SEND;
        return false;
    }
    f_name = parts[4];

    f_id = id;

    return true;
}


computer::priority_t computer::get_priority() const
{
    return f_priority;
}


void computer::set_start_time(snapdev::timespec_ex const & start_time)
{
    f_start_time = start_time;
}


snapdev::timespec_ex const & computer::get_start_time() const
{
    return f_start_time;
}


std::string const & computer::get_name() const
{
    return f_name;
}


std::string const & computer::get_id() const
{
    if(f_id.empty())
    {
        if(f_priority == PRIORITY_UNDEFINED)
        {
            throw cluck::invalid_parameter("computer::get_id() can't be called when the priority is not defined.");
        }
        if(f_ip_address.is_default())
        {
            throw cluck::invalid_parameter("computer::get_id() can't be called when the address is the default address.");
        }
        if(f_pid == 0)
        {
            throw cluck::invalid_parameter("computer::get_id() can't be called when the pid is not defined.");
        }

        std::stringstream ss;
        ss
            << std::setfill('0') << std::setw(2) << static_cast<int>(f_priority)
            << '|'
            << f_random_id
            << '|'
            << f_ip_address.to_ipv4or6_string(addr::STRING_IP_ADDRESS | addr::STRING_IP_BRACKET_ADDRESS)
            << '|'
            << f_pid
            << '|'
            << f_name;
        f_id = ss.str();
    }

    return f_id;
}


addr::addr const & computer::get_ip_address() const
{
    return f_ip_address;
}


} // namespace cluck_daemon
// vim: ts=4 sw=4 et
