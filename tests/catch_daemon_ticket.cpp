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
#include    "catch_main.h"



// daemon
//
#include    <daemon/ticket.h>

#include    <daemon/cluckd.h>


// cluck
//
//#include    <cluck/cluck.h>
//#include    <cluck/exception.h>
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
#include    <snapdev/gethostname.h>
//#include    <snapdev/enum_class_math.h>
//#include    <snapdev/escape_special_regex_characters.h>


// C++
//
//#include    <regex>


// last include
//
#include    <snapdev/poison.h>



namespace
{



char const * g_argv[2] = {
    "catch_daemon_ticket",
    nullptr
};


class cluckd_mock
    : public cluck_daemon::cluckd
{
public:
    cluckd_mock();

private:
};


cluckd_mock::cluckd_mock()
    : cluckd(1, const_cast<char **>(g_argv))
{
}





//addr::addr get_random_address()
//{
//    addr::addr a;
//    sockaddr_in ip = {
//        .sin_family = AF_INET,
//        .sin_port = htons(20002),
//        .sin_addr = {
//            .s_addr = 0,
//        },
//        .sin_zero = {},
//    };
//    SNAP_CATCH2_NAMESPACE::random(ip.sin_addr.s_addr);
//    a.set_ipv4(ip);
//    return a;
//}



} // no name namespace



CATCH_TEST_CASE("daemon_ticket", "[cluckd][ticket][daemon]")
{
    CATCH_START_SECTION("daemon_ticket: verify defaults")
    {
        cluck::timeout_t obtention_timeout(snapdev::now());
        obtention_timeout += cluck::timeout_t(5, 0);
        cluckd_mock d;
        cluck_daemon::ticket t(
              &d
            , nullptr
            , "ticket_test"
            , 123
            , "rc/5003"
            , obtention_timeout
            , cluck::timeout_t(10, 0)
            , "rc"
            , "website");

        CATCH_REQUIRE(t.get_owner() == snapdev::gethostname());
        t.set_owner("rc3");
        CATCH_REQUIRE(t.get_owner() == "rc3");

        CATCH_REQUIRE(t.get_client_pid() == 5003);

        CATCH_REQUIRE(t.get_serial() == cluck_daemon::ticket::NO_SERIAL);
        t.set_serial(93);
        CATCH_REQUIRE(t.get_serial() == 93);

        CATCH_REQUIRE(t.get_unlock_duration() == cluck::timeout_t(10, 0));
        t.set_unlock_duration(cluck::timeout_t(3, 500000000));
        CATCH_REQUIRE(t.get_unlock_duration() == cluck::timeout_t(3, 500000000));

        CATCH_REQUIRE(t.get_ticket_number() == cluck_daemon::ticket::NO_TICKET);
        t.set_ticket_number(435);
        CATCH_REQUIRE(t.get_ticket_number() == 435);

        CATCH_REQUIRE_FALSE(t.is_locked());

        //CATCH_REQUIRE(t.one_leader()); -- TBD: this requires cluckd info

        CATCH_REQUIRE(t.get_obtention_timeout() == obtention_timeout);

        CATCH_REQUIRE(t.get_lock_duration() == cluck::timeout_t(10, 0));

        // the result of this function varies depending on the current
        // state (i.e. ALIVE, LOCKED, WAITING) -- at the moment we are
        // waiting so it returns the obtention timeout
        //
        CATCH_REQUIRE(t.get_current_timeout_date() == obtention_timeout);

        // the following can fail if your computer is really really slow
        //
        CATCH_REQUIRE_FALSE(t.timed_out());

        CATCH_REQUIRE(t.get_object_name() == "ticket_test");
        CATCH_REQUIRE(t.get_tag() == 123);
        CATCH_REQUIRE(t.get_server_name() == "rc");
        CATCH_REQUIRE(t.get_service_name() == "website");
        CATCH_REQUIRE(t.get_entering_key() == "rc/5003");
        CATCH_REQUIRE(t.get_ticket_key() == "000001b3/rc/5003");

        std::string const blob(t.serialize());

        cluck_daemon::ticket t2(
              &d
            , nullptr
            , "ticket_test"
            , ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG
            , "rc/5003"
            , cluck::CLUCK_DEFAULT_TIMEOUT + snapdev::now()
            , cluck::CLUCK_DEFAULT_TIMEOUT
            , ""
            , "");
        t2.unserialize(blob);

        // now t2 is (mostly) equal to t1
        //
        CATCH_REQUIRE(t2.get_owner() == "rc3");

        CATCH_REQUIRE(t2.get_client_pid() == 5003);

        CATCH_REQUIRE(t2.get_serial() == 93);

        CATCH_REQUIRE(t2.get_unlock_duration() == cluck::timeout_t(3, 500000000));

        CATCH_REQUIRE(t2.get_ticket_number() == 435);

        CATCH_REQUIRE_FALSE(t2.is_locked());

        CATCH_REQUIRE(t2.get_obtention_timeout() == obtention_timeout);

        CATCH_REQUIRE(t2.get_lock_duration() == cluck::timeout_t(10, 0));

        CATCH_REQUIRE(t2.get_current_timeout_date() == obtention_timeout);

        CATCH_REQUIRE_FALSE(t2.timed_out());

        CATCH_REQUIRE(t2.get_object_name() == "ticket_test");
        CATCH_REQUIRE(t2.get_tag() == 123);
        CATCH_REQUIRE(t2.get_server_name() == "rc");
        CATCH_REQUIRE(t2.get_service_name() == "website");
        CATCH_REQUIRE(t2.get_entering_key() == "rc/5003");
        CATCH_REQUIRE(t2.get_ticket_key() == "000001b3/rc/5003");
    }
    CATCH_END_SECTION()
}


//CATCH_TEST_CASE("daemon_computer_errors", "[cluckd][computer][daemon][error]")
//{
//    CATCH_START_SECTION("daemon_computer_errors: empty name")
//    {
//        CATCH_REQUIRE_THROWS_MATCHES(
//              cluck_daemon::computer("", 5, addr::addr())
//            , cluck::invalid_parameter
//            , Catch::Matchers::ExceptionMessage("cluck_exception: the computer name cannot be an empty string."));
//    }
//    CATCH_END_SECTION()
//
//    CATCH_START_SECTION("daemon_computer_errors: invalid character in name")
//    {
//        // the | character is an issue in the serializer
//        //
//        CATCH_REQUIRE_THROWS_MATCHES(
//              cluck_daemon::computer("|pipe-not-allowed|", 5, addr::addr())
//            , cluck::invalid_parameter
//            , Catch::Matchers::ExceptionMessage("cluck_exception: a computer name cannot include the '|' or null characters."));
//
//        // the '\0' is not allowed in the name
//        //
//        std::string n("start");
//        n += '\0';
//        n += "end";
//        CATCH_REQUIRE_THROWS_MATCHES(
//              cluck_daemon::computer(n, 5, addr::addr())
//            , cluck::invalid_parameter
//            , Catch::Matchers::ExceptionMessage("cluck_exception: a computer name cannot include the '|' or null characters."));
//    }
//    CATCH_END_SECTION()
//
//    CATCH_START_SECTION("daemon_computer_errors: serialized ID must be 5 parts")
//    {
//        cluck_daemon::computer c;
//        CATCH_REQUIRE_FALSE(c.set_id("need|5|parts"));
//    }
//    CATCH_END_SECTION()
//
//    CATCH_START_SECTION("daemon_computer_errors: invalid priority with constructor")
//    {
//        // too small
//        CATCH_REQUIRE_THROWS_MATCHES(
//              cluck_daemon::computer("test", cluck_daemon::computer::PRIORITY_USER_MIN - 1, addr::addr())
//            , cluck::invalid_parameter
//            , Catch::Matchers::ExceptionMessage(
//                  "cluck_exception: priority is limited to a number between "
//                + std::to_string(static_cast<int>(cluck_daemon::computer::PRIORITY_USER_MIN))
//                + " and "
//                + std::to_string(static_cast<int>(cluck_daemon::computer::PRIORITY_MAX))
//                + " inclusive."));
//
//        // too large
//        CATCH_REQUIRE_THROWS_MATCHES(
//              cluck_daemon::computer("test", cluck_daemon::computer::PRIORITY_MAX + 1, addr::addr())
//            , cluck::invalid_parameter
//            , Catch::Matchers::ExceptionMessage(
//                  "cluck_exception: priority is limited to a number between "
//                + std::to_string(static_cast<int>(cluck_daemon::computer::PRIORITY_USER_MIN))
//                + " and "
//                + std::to_string(static_cast<int>(cluck_daemon::computer::PRIORITY_MAX))
//                + " inclusive."));
//    }
//    CATCH_END_SECTION()
//
//    CATCH_START_SECTION("daemon_computer_errors: invalid priority in id string")
//    {
//        // not a number
//        {
//            cluck_daemon::computer c;
//            CATCH_REQUIRE_FALSE(c.set_id("prio|123|127.0.0.1|5501|name"));
//        }
//
//        // too small
//        {
//            cluck_daemon::computer c;
//            std::string too_small(std::to_string(cluck_daemon::computer::PRIORITY_USER_MIN - 1));
//            CATCH_REQUIRE_FALSE(c.set_id(too_small + "|123|127.0.0.1|5501|name"));
//        }
//
//        // too large
//        {
//            cluck_daemon::computer c;
//            std::string too_large(std::to_string(cluck_daemon::computer::PRIORITY_MAX + 1));
//            CATCH_REQUIRE_FALSE(c.set_id(too_large + "|123|127.0.0.1|5501|name"));
//        }
//    }
//    CATCH_END_SECTION()
//
//    CATCH_START_SECTION("daemon_computer_errors: invalid random number in id string")
//    {
//        // not a number
//        {
//            cluck_daemon::computer c;
//            CATCH_REQUIRE_FALSE(c.set_id("10|random|127.0.0.1|5501|name"));
//        }
//    }
//    CATCH_END_SECTION()
//
//    CATCH_START_SECTION("daemon_computer_errors: invalid IP address in id string")
//    {
//        // empty
//        {
//            cluck_daemon::computer c;
//            CATCH_REQUIRE_FALSE(c.set_id("10|9001||5501|name"));
//        }
//
//        // not an IP
//        {
//            cluck_daemon::computer c;
//            CATCH_REQUIRE_FALSE(c.set_id("10|9001|not an IP|5501|name"));
//        }
//
//        // "default"
//        {
//            cluck_daemon::computer c;
//            CATCH_REQUIRE_FALSE(c.set_id("10|9001|0.0.0.0|5501|name"));
//
//            // this is a trick that works at the moment; we set a valid
//            // priority, so next we can have an invalid address that
//            // remains... because the set_id() still makes updates to
//            // the object even if it generates an error
//            //
//            CATCH_REQUIRE_THROWS_MATCHES(
//                  c.get_id()
//                , cluck::invalid_parameter
//                , Catch::Matchers::ExceptionMessage("cluck_exception: computer::get_id() can't be called when the address is the default address."));
//        }
//    }
//    CATCH_END_SECTION()
//
//    CATCH_START_SECTION("daemon_computer_errors: invalid PID")
//    {
//        // empty
//        {
//            cluck_daemon::computer c;
//            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1||name"));
//        }
//
//        // zero
//        {
//            cluck_daemon::computer c;
//            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1|0|name"));
//
//            // the following is a trick that currently works because the
//            // set_id() function saves the data until the one part that
//            // is invalid
//            //
//            CATCH_REQUIRE_THROWS_MATCHES(
//                  c.get_id()
//                , cluck::invalid_parameter
//                , Catch::Matchers::ExceptionMessage("cluck_exception: computer::get_id() can't be called when the pid is not defined."));
//        }
//
//        // negative
//        {
//            cluck_daemon::computer c;
//            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1|-5501|name"));
//        }
//
//        // too large
//        {
//            cluck_daemon::computer c;
//            std::string const count(std::to_string(cppthread::get_pid_max() + 1));
//            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1|" + count + "|name"));
//        }
//    }
//    CATCH_END_SECTION()
//
//    CATCH_START_SECTION("daemon_computer_errors: invalid name")
//    {
//        // empty
//        {
//            cluck_daemon::computer c;
//            CATCH_REQUIRE_FALSE(c.set_id("10|9001|127.0.0.1|2512|"));
//        }
//    }
//    CATCH_END_SECTION()
//}



// vim: ts=4 sw=4 et
