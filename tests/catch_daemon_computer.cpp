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
#include    "catch_main.h"



// daemon
//
#include    <daemon/computer.h>


// cluck
//
//#include    <cluck/cluck.h>
#include    <cluck/exception.h>
//#include    <cluck/names.h>
//#include    <cluck/version.h>


// eventdispatcher
//
//#include    <eventdispatcher/communicator.h>
//#include    <eventdispatcher/dispatcher.h>
//#include    <eventdispatcher/names.h>
//#include    <eventdispatcher/tcp_client_permanent_message_connection.h>
//
//#include    <eventdispatcher/reporter/executor.h>
//#include    <eventdispatcher/reporter/lexer.h>
//#include    <eventdispatcher/reporter/parser.h>
//#include    <eventdispatcher/reporter/state.h>


// cppthread
//
#include    <cppthread/thread.h>


// snapdev
//
#include    <snapdev/enum_class_math.h>
#include    <snapdev/escape_special_regex_characters.h>


// C++
//
#include    <regex>


// last include
//
#include    <snapdev/poison.h>



namespace
{



addr::addr get_random_address()
{
    addr::addr a;
    sockaddr_in ip = {
        .sin_family = AF_INET,
        .sin_port = htons(20002),
        .sin_addr = {
            .s_addr = 0,
        },
        .sin_zero = {},
    };
    SNAP_CATCH2_NAMESPACE::random(ip.sin_addr.s_addr);
    a.set_ipv4(ip);
    return a;
}



} // no name namespace



CATCH_TEST_CASE("daemon_computer", "[cluckd][computer][daemon]")
{
    CATCH_START_SECTION("daemon_computer: verify defaults")
    {
        cluck_daemon::computer c;

        CATCH_REQUIRE_FALSE(c.is_self());
        CATCH_REQUIRE(c.get_connected());
        CATCH_REQUIRE(c.get_priority() == cluck_daemon::computer::PRIORITY_UNDEFINED);
        CATCH_REQUIRE(c.get_start_time() == snapdev::timespec_ex());
        CATCH_REQUIRE(c.get_name() == std::string());
        CATCH_REQUIRE(c.get_ip_address() == addr::addr());

        // without a priority, this one fails
        //
        CATCH_REQUIRE_THROWS_MATCHES(
              c.get_id()
            , cluck::invalid_parameter
            , Catch::Matchers::ExceptionMessage("cluck_exception: computer::get_id() can't be called when the priority is not defined."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_computer: verify self defaults")
    {
        // the LEADER priority cannot get saved inside the computer object
        //
        for(cluck_daemon::computer::priority_t p(cluck_daemon::computer::PRIORITY_USER_MIN);
            p <= cluck_daemon::computer::PRIORITY_MAX;
            ++p)
        {
            CATCH_REQUIRE(p != cluck_daemon::computer::PRIORITY_LEADER);

            std::string n;
            do
            {
                n = SNAP_CATCH2_NAMESPACE::random_string(1, 15);
            }
            while(std::strchr(n.c_str(), '|') != nullptr);
            addr::addr a(get_random_address());
            cluck_daemon::computer c(n, p, a);

            CATCH_REQUIRE(c.is_self());
            CATCH_REQUIRE(c.get_connected());
            CATCH_REQUIRE(c.get_priority() == p);
            CATCH_REQUIRE(c.get_start_time() == snapdev::timespec_ex());
            CATCH_REQUIRE(c.get_name() == n);
            CATCH_REQUIRE(c.get_ip_address() == a);

            // with a priority, this one works
            //
            std::string const id(c.get_id());
            CATCH_REQUIRE(c.get_id() == id);        // ID does not change after first call

            // the ID is a string defined as:
            //    <priority> | <random ID> | <IP> | <pid> | <name>
            //
            // we do not have access to the <random ID> part so we use a
            // regex to make sure that the ID is defined as expected
            //
            std::string expr;
            if(p < 10)
            {
                expr += '0';
            }
            expr += std::to_string(p);
            expr += "\\|[0-9]+\\|";
            expr += snapdev::escape_special_regex_characters(a.to_ipv4or6_string(addr::STRING_IP_ADDRESS | addr::STRING_IP_BRACKET_ADDRESS));
            expr += "\\|";
            expr += std::to_string(getpid());
            expr += "\\|";
            expr += snapdev::escape_special_regex_characters(n);
            std::regex const compiled_regex(expr);
            CATCH_REQUIRE(std::regex_match(id, compiled_regex));

            cluck_daemon::computer copy;
            CATCH_REQUIRE_FALSE(copy.is_self());
            CATCH_REQUIRE(copy.get_connected());
            CATCH_REQUIRE(copy.get_priority() == cluck_daemon::computer::PRIORITY_UNDEFINED);
            CATCH_REQUIRE(copy.get_start_time() == snapdev::timespec_ex());
            CATCH_REQUIRE(copy.get_name() == std::string());
            CATCH_REQUIRE(copy.get_ip_address() == addr::addr());

            CATCH_REQUIRE(copy.set_id(id));
            CATCH_REQUIRE(copy.get_id() == id);

            CATCH_REQUIRE_FALSE(copy.is_self());
            CATCH_REQUIRE(copy.get_connected());
            CATCH_REQUIRE(copy.get_priority() == p);
            CATCH_REQUIRE(copy.get_start_time() == snapdev::timespec_ex());
            CATCH_REQUIRE(copy.get_name() == n);
            CATCH_REQUIRE(copy.get_ip_address() == a);

            // trying to set the ID again fails
            //
            CATCH_REQUIRE_THROWS_MATCHES(
                  copy.set_id(id)
                , cluck::logic_error
                , Catch::Matchers::ExceptionMessage("logic_error: computer::set_id() cannot be called more than once."));
        }
    }
    CATCH_END_SECTION()
}


CATCH_TEST_CASE("daemon_computer_errors", "[cluckd][computer][daemon][error]")
{
    CATCH_START_SECTION("daemon_computer_errors: empty name")
    {
        CATCH_REQUIRE_THROWS_MATCHES(
              cluck_daemon::computer("", 5, addr::addr())
            , cluck::invalid_parameter
            , Catch::Matchers::ExceptionMessage("cluck_exception: the computer name cannot be an empty string."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_computer_errors: invalid character in name")
    {
        // the | character is an issue in the serializer
        //
        CATCH_REQUIRE_THROWS_MATCHES(
              cluck_daemon::computer("|pipe-not-allowed|", 5, addr::addr())
            , cluck::invalid_parameter
            , Catch::Matchers::ExceptionMessage("cluck_exception: a computer name cannot include the '|' or null characters."));

        // the '\0' is not allowed in the name
        //
        std::string n("start");
        n += '\0';
        n += "end";
        CATCH_REQUIRE_THROWS_MATCHES(
              cluck_daemon::computer(n, 5, addr::addr())
            , cluck::invalid_parameter
            , Catch::Matchers::ExceptionMessage("cluck_exception: a computer name cannot include the '|' or null characters."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_computer_errors: serialized ID must be 5 parts")
    {
        cluck_daemon::computer c;
        CATCH_REQUIRE_FALSE(c.set_id("need|5|parts"));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_computer_errors: invalid priority with constructor")
    {
        // too small
        CATCH_REQUIRE_THROWS_MATCHES(
              cluck_daemon::computer("test", cluck_daemon::computer::PRIORITY_USER_MIN - 1, addr::addr())
            , cluck::invalid_parameter
            , Catch::Matchers::ExceptionMessage(
                  "cluck_exception: priority is limited to a number between "
                + std::to_string(static_cast<int>(cluck_daemon::computer::PRIORITY_USER_MIN))
                + " and "
                + std::to_string(static_cast<int>(cluck_daemon::computer::PRIORITY_MAX))
                + " inclusive."));

        // too large
        CATCH_REQUIRE_THROWS_MATCHES(
              cluck_daemon::computer("test", cluck_daemon::computer::PRIORITY_MAX + 1, addr::addr())
            , cluck::invalid_parameter
            , Catch::Matchers::ExceptionMessage(
                  "cluck_exception: priority is limited to a number between "
                + std::to_string(static_cast<int>(cluck_daemon::computer::PRIORITY_USER_MIN))
                + " and "
                + std::to_string(static_cast<int>(cluck_daemon::computer::PRIORITY_MAX))
                + " inclusive."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_computer_errors: invalid priority in id string")
    {
        // not a number
        {
            cluck_daemon::computer c;
            CATCH_REQUIRE_FALSE(c.set_id("prio|123|127.0.0.1|5501|name"));
        }

        // too small
        {
            cluck_daemon::computer c;
            std::string too_small(std::to_string(cluck_daemon::computer::PRIORITY_USER_MIN - 1));
            CATCH_REQUIRE_FALSE(c.set_id(too_small + "|123|127.0.0.1|5501|name"));
        }

        // too large
        {
            cluck_daemon::computer c;
            std::string too_large(std::to_string(cluck_daemon::computer::PRIORITY_MAX + 1));
            CATCH_REQUIRE_FALSE(c.set_id(too_large + "|123|127.0.0.1|5501|name"));
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_computer_errors: invalid random number in id string")
    {
        // not a number
        {
            cluck_daemon::computer c;
            CATCH_REQUIRE_FALSE(c.set_id("10|random|127.0.0.1|5501|name"));
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_computer_errors: invalid IP address in id string")
    {
        // empty
        {
            cluck_daemon::computer c;
            CATCH_REQUIRE_FALSE(c.set_id("10|9001||5501|name"));
        }

        // not an IP
        {
            cluck_daemon::computer c;
            CATCH_REQUIRE_FALSE(c.set_id("10|9001|not an IP|5501|name"));
        }

        // "default"
        {
            cluck_daemon::computer c;
            CATCH_REQUIRE_FALSE(c.set_id("10|9001|0.0.0.0|5501|name"));

            // this is a trick that works at the moment; we set a valid
            // priority, so next we can have an invalid address that
            // remains... because the set_id() still makes updates to
            // the object even if it generates an error
            //
            CATCH_REQUIRE_THROWS_MATCHES(
                  c.get_id()
                , cluck::invalid_parameter
                , Catch::Matchers::ExceptionMessage("cluck_exception: computer::get_id() can't be called when the address is the default address."));
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_computer_errors: invalid PID")
    {
        // empty
        {
            cluck_daemon::computer c;
            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1||name"));
        }

        // zero
        {
            cluck_daemon::computer c;
            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1|0|name"));

            // the following is a trick that currently works because the
            // set_id() function saves the data until the one part that
            // is invalid
            //
            CATCH_REQUIRE_THROWS_MATCHES(
                  c.get_id()
                , cluck::invalid_parameter
                , Catch::Matchers::ExceptionMessage("cluck_exception: computer::get_id() can't be called when the pid is not defined."));
        }

        // negative
        {
            cluck_daemon::computer c;
            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1|-5501|name"));
        }

        // too large
        {
            cluck_daemon::computer c;
            std::string const count(std::to_string(cppthread::get_pid_max() + 1));
            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1|" + count + "|name"));
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_computer_errors: invalid name")
    {
        // empty
        {
            cluck_daemon::computer c;
            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1|2512|"));
        }
    }
    CATCH_END_SECTION()
}



// vim: ts=4 sw=4 et
