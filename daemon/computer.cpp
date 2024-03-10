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


computer::computer(std::string const & name, priority_t priority)
    : f_self(true)
    , f_priority(priority)
    , f_pid(getpid())
    , f_name(name)
{
    RAND_bytes(reinterpret_cast<unsigned char *>(&f_random_id), sizeof(f_random_id));

    //snap::snap_config config("snapcommunicator");
    //f_ip_address = config["listen"];
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
            << "received a computer id which does not have exactly 5 parts."
            << SNAP_LOG_SEND;
        return false;
    }

    // base is VERY IMPORTANT for this one as we save priorities below ten
    // as 0n (01 to 09) so the sort works as expected
    //
    std::int64_t value(0);
    advgetopt::validator_integer::convert_string(parts[0], value);
    if(value < PRIORITY_USER_MIN
    || value > PRIORITY_MAX)
    {
        SNAP_LOG_ERROR
            << "priority is limited to a number between "
            << PRIORITY_USER_MIN
            << " and "
            << PRIORITY_MAX
            << " inclusive."
            << SNAP_LOG_SEND;
        return false;
    }
    f_priority = value;

    advgetopt::validator_integer::convert_string(parts[1], value);
    f_random_id = value;

    f_ip_address = parts[2];
    if(f_ip_address.empty())
    {
        SNAP_LOG_ERROR
            << "the process IP cannot be an empty string."
            << SNAP_LOG_SEND;
        return false;
    }

    advgetopt::validator_integer::convert_string(parts[3], value);
    f_pid = value;
    if(f_pid < 1
    || f_pid > cppthread::get_pid_max())
    {
        SNAP_LOG_ERROR
            << "process identifier "
            << f_pid
            << " is out of bounds: [0.."
            << cppthread::get_pid_max()
            << "]."
            << SNAP_LOG_SEND;
        return false;
    }

    f_name = parts[4];
    if(f_name.empty())
    {
        SNAP_LOG_ERROR
            << "the server name in the lock identifier cannot be empty."
            << SNAP_LOG_SEND;
        return false;
    }

    f_id = id;

    return true;
}


computer::priority_t computer::get_priority() const
{
    return f_priority;
}


void computer::set_start_time(time_t start_time)
{
    f_start_time = start_time;
}


time_t computer::get_start_time() const
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
            throw cluck::invalid_parameter("computer::get_id() can't be called when the priority is not defined");
        }
        if(f_ip_address.empty())
        {
            throw cluck::invalid_parameter("computer::get_id() can't be called when the address is empty");
        }
        if(f_pid == 0)
        {
            throw cluck::invalid_parameter("computer::get_id() can't be called when the pid is not defined");
        }

        std::stringstream ss;
        ss
            << std::setfill('0') << std::setw(2) << f_priority
            << '|'
            << f_random_id
            << '|'
            << f_ip_address
            << '|'
            << f_pid
            << '|'
            << f_name;
        f_id = ss.str();
    }

    return f_id;
}


std::string const & computer::get_ip_address() const
{
    return f_ip_address;
}


} // namespace cluck_daemon
// vim: ts=4 sw=4 et
