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
#define private public
#include    <daemon/ticket.h>
#undef private

#include    <daemon/cluckd.h>


// cluck
//
#include    <cluck/exception.h>
#include    <cluck/version.h>


// cppthread
//
#include    <cppthread/thread.h>


// snapdev
//
#include    <snapdev/gethostname.h>
#include    <snapdev/stringize.h>


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


class messenger_mock
    : public cluck_daemon::messenger
{
public:
    typedef std::shared_ptr<messenger_mock>             pointer_t;

                                messenger_mock(cluck_daemon::cluckd * c, advgetopt::getopt & opts);

    // connection_with_send_message implementation
    //
    virtual bool                send_message(ed::message & msg, bool cache = false) override;
    void                        add_expected_message(ed::message & msg);

private:
    ed::message::list_t         f_expected_message = ed::message::list_t();
};


messenger_mock::messenger_mock(cluck_daemon::cluckd * c, advgetopt::getopt & opts)
    : messenger(c, opts)
{
}


bool messenger_mock::send_message(ed::message & msg, bool cache)
{
    SNAP_LOG_INFO
        << "mock messenger captured message \""
        << msg
        << "\"."
        << SNAP_LOG_SEND;

    CATCH_REQUIRE_FALSE(cache);

    CATCH_REQUIRE_FALSE(f_expected_message.empty());

    CATCH_REQUIRE(f_expected_message.front().get_sent_from_server() == msg.get_sent_from_server());
    CATCH_REQUIRE(f_expected_message.front().get_sent_from_service() == msg.get_sent_from_service());
    CATCH_REQUIRE(f_expected_message.front().get_server() == msg.get_server());
    CATCH_REQUIRE(f_expected_message.front().get_service() == msg.get_service());
    CATCH_REQUIRE(f_expected_message.front().get_command() == msg.get_command());

    for(auto p : f_expected_message.front().get_all_parameters())
    {
        CATCH_REQUIRE(msg.has_parameter(p.first));
        CATCH_REQUIRE(msg.get_parameter(p.first) == p.second);
    }

    f_expected_message.pop_front();

    return true;
}


void messenger_mock::add_expected_message(ed::message & msg)
{
    f_expected_message.push_back(msg);
}



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

    CATCH_START_SECTION("daemon_ticket: test set_alive_timeout()")
    {
        cluck::timeout_t const now(snapdev::now());
        cluck::timeout_t const obtention_timeout = now + cluck::timeout_t(5, 0);
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

        CATCH_REQUIRE(t.get_current_timeout_date() == obtention_timeout);

        // set a valid date between now & obtention timeout
        //
        cluck::timeout_t const alive_timeout(now + cluck::timeout_t(2, 500'000'000));
        t.set_alive_timeout(alive_timeout);
        CATCH_REQUIRE(t.get_current_timeout_date() == alive_timeout);

        // if negative, use 0 instead--this resets back to the obtention timeout
        //
        t.set_alive_timeout(cluck::timeout_t(-10, 345'637'291));
        CATCH_REQUIRE(t.get_current_timeout_date() == obtention_timeout);

        // reuse the valid date to make sure the next version works
        //
        t.set_alive_timeout(alive_timeout);
        CATCH_REQUIRE(t.get_current_timeout_date() == alive_timeout);

        // explicitly set to 0
        //
        t.set_alive_timeout(cluck::timeout_t(0, 0));
        CATCH_REQUIRE(t.get_current_timeout_date() == obtention_timeout);

        // reset one more time
        //
        t.set_alive_timeout(alive_timeout);
        CATCH_REQUIRE(t.get_current_timeout_date() == alive_timeout);

        // if the alive timeout is after obtention, the set uses the obtention
        // timeout instead
        //
        t.set_alive_timeout(obtention_timeout + cluck::timeout_t(3, 409'453'112));
        CATCH_REQUIRE(t.get_current_timeout_date() == obtention_timeout);
    }
    CATCH_END_SECTION()
}


CATCH_TEST_CASE("daemon_ticket_errors", "[cluckd][ticket][daemon][error]")
{
    CATCH_START_SECTION("daemon_ticket_errors: call set_ticket_number() twice")
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

        // the first time, it goes through as expected
        //
        t.set_ticket_number(123);
        CATCH_REQUIRE(t.get_ticket_number() == 123);
        CATCH_REQUIRE(t.get_ticket_key() == "0000007b/rc/5003");

        // the second time, we detect that it is already defined
        //
        CATCH_REQUIRE_THROWS_MATCHES(
              t.set_ticket_number(123)
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage(
                  "logic_error: ticket::set_ticket_number() called with "
                + std::to_string(123)
                + " when f_our_ticket is already set to "
                + std::to_string(123)
                + "."));

        // reset back to zero is also not possible
        //
        CATCH_REQUIRE_THROWS_MATCHES(
              t.set_ticket_number(cluck_daemon::ticket::NO_TICKET)
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage(
                  "logic_error: ticket::set_ticket_number() called with "
                + std::to_string(cluck_daemon::ticket::NO_TICKET)
                + " when f_our_ticket is already set to "
                + std::to_string(123)
                + "."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_ticket_errors: ticket number wrap around in max_ticket()")
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

        CATCH_REQUIRE_THROWS_MATCHES(
              t.max_ticket(std::numeric_limits<cluck_daemon::ticket::ticket_id_t>::max())
            , cluck::out_of_range
            , Catch::Matchers::ExceptionMessage(
                  "out_of_range: ticket::max_ticket() tried to generate the next ticket and got a wrapping around number."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_ticket_errors: ticket with bad entering key")
    {
        cluck::timeout_t obtention_timeout(snapdev::now());
        obtention_timeout += cluck::timeout_t(5, 0);
        cluckd_mock d;
        cluck_daemon::ticket t(
              &d
            , nullptr
            , "ticket_test"
            , 123
            , "bad_entering_key"
            , obtention_timeout
            , cluck::timeout_t(10, 0)
            , "rc"
            , "website");

        CATCH_REQUIRE_THROWS_MATCHES(
              t.get_client_pid()
            , cluck::invalid_parameter
            , Catch::Matchers::ExceptionMessage(
                "cluck_exception: ticket::get_client_pid() split f_entering_key "
                "\"bad_entering_key\" and did not get exactly two segments."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("daemon_ticket_errors: lock_failed()")
    {
        advgetopt::option const options[] =
        {
            advgetopt::define_option(
                  advgetopt::Name("candidate-priority")
                , advgetopt::ShortName('p')
                , advgetopt::Flags(advgetopt::all_flags<
                              advgetopt::GETOPT_FLAG_REQUIRED
                            , advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
                , advgetopt::Help("Define the priority of this candidate (1 to 14) to gain a leader position or \"off\".")
                , advgetopt::DefaultValue("14")
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

        advgetopt::options_environment const options_environment =
        {
            .f_project_name = "cluckd",
            .f_group_name = "cluck",
            .f_options = options,
            .f_environment_variable_name = "CLUCKD_OPTIONS",
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
        };


        cluck::timeout_t obtention_timeout(snapdev::now());
        obtention_timeout += cluck::timeout_t(5, 0);
        cluckd_mock d;
        char const * argv[] = { "test-ticket" };
        advgetopt::getopt opts(options_environment, 1, const_cast<char **>(argv));
        messenger_mock::pointer_t m(std::make_shared<messenger_mock>(&d, opts));
        cluck_daemon::ticket t(
              &d
            , m
            , "ticket_test"
            , 123
            , "rc/5003"
            , obtention_timeout
            , cluck::timeout_t(10, 0)
            , "rc"
            , "website");

        ed::message lock_failed;
        lock_failed.set_server("rc");
        lock_failed.set_service("website");
        lock_failed.set_command("LOCK_FAILED");
        lock_failed.add_parameter("description", "ticket failed before or after the lock was obtained (no reason)");
        lock_failed.add_parameter("error", "failed");
        lock_failed.add_parameter("key", "rc/5003");
        lock_failed.add_parameter("object_name", "ticket_test");
        lock_failed.add_parameter("tag", 123);
        m->add_expected_message(lock_failed);
        t.lock_failed("no reason");

        {
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
            CATCH_REQUIRE(t2.f_lock_failed == cluck_daemon::ticket::lock_failure_t::LOCK_FAILURE_NONE);
            t2.unserialize(blob);
            CATCH_REQUIRE(t2.f_lock_failed == cluck_daemon::ticket::lock_failure_t::LOCK_FAILURE_LOCK);
        }

        t.lock_failed("ignore");

        {
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
            CATCH_REQUIRE(t2.f_lock_failed == cluck_daemon::ticket::lock_failure_t::LOCK_FAILURE_NONE);
            t2.unserialize(blob);
            CATCH_REQUIRE(t2.f_lock_failed == cluck_daemon::ticket::lock_failure_t::LOCK_FAILURE_UNLOCKING);
        }

        t.lock_failed("further");

        {
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
            CATCH_REQUIRE(t2.f_lock_failed == cluck_daemon::ticket::lock_failure_t::LOCK_FAILURE_NONE);
            t2.unserialize(blob);
            CATCH_REQUIRE(t2.f_lock_failed == cluck_daemon::ticket::lock_failure_t::LOCK_FAILURE_UNLOCKING);
        }

        t.lock_failed("calls");

        {
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
            CATCH_REQUIRE(t2.f_lock_failed == cluck_daemon::ticket::lock_failure_t::LOCK_FAILURE_NONE);
            t2.unserialize(blob);
            CATCH_REQUIRE(t2.f_lock_failed == cluck_daemon::ticket::lock_failure_t::LOCK_FAILURE_UNLOCKING);
        }
    }
    CATCH_END_SECTION()
}



// vim: ts=4 sw=4 et
