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



// cluckd
//
#include    <daemon/cluckd.h>


// cluck
//
#include    <cluck/exception.h>


// eventdispatcher
//
#include    <eventdispatcher/reporter/executor.h>
#include    <eventdispatcher/reporter/parser.h>
#include    <eventdispatcher/reporter/variable_integer.h>


// snapdev
//
#include    <snapdev/gethostname.h>


// advgetopt
//
#include    <advgetopt/exception.h>


// last include
//
#include    <snapdev/poison.h>



namespace
{



addr::addr get_address()
{
    addr::addr a;
    sockaddr_in ip = {
        .sin_family = AF_INET,
        .sin_port = htons(20002),
        .sin_addr = {
            .s_addr = htonl(0x7f000001),
        },
        .sin_zero = {},
    };
    a.set_ipv4(ip);
    return a;
}



} // no name namespace



CATCH_TEST_CASE("cluck_daemon_one_computer", "[cluckd][daemon]")
{
    CATCH_START_SECTION("cluck_daemon_one_computer: verify cluckd")
    {
        addr::addr a(get_address());

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        // no elections happened, 'lock' is not a leader
        //
        CATCH_REQUIRE(lock->is_leader() == nullptr);
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_a()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_a(): only a leader can call this function."));
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_b()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_b(): only a leader can call this function."));

        // messenger is not yet connected, it's not ready
        //
        CATCH_REQUIRE_FALSE(lock->is_daemon_ready());

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/cluck_daemon_test_one_computer.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (1 cluckd): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()
}


CATCH_TEST_CASE("cluck_daemon_two_computers", "[cluckd][daemon]")
{
    CATCH_START_SECTION("cluck_daemon_two_computers: verify cluckd")
    {
        addr::addr a(get_address());

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",

            "--server-name",
            snapdev::gethostname(),
            "--candidate-priority",
            "10",
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        // no elections happened, 'lock' is not a leader
        //
        CATCH_REQUIRE(lock->is_leader() == nullptr);
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_a()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_a(): only a leader can call this function."));
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_b()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_b(): only a leader can call this function."));

        // messenger is not yet connected, it's not ready
        //
        CATCH_REQUIRE_FALSE(lock->is_daemon_ready());

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/cluck_daemon_test_two_computers.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (2 cluckd): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()
}


CATCH_TEST_CASE("cluck_daemon_three_computers", "[cluckd][daemon]")
{
    CATCH_START_SECTION("cluck_daemon_three_computers: verify cluckd")
    {
        addr::addr a(get_address());

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        // no elections happened, 'lock' is not a leader
        //
        CATCH_REQUIRE(lock->is_leader() == nullptr);
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_a()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_a(): only a leader can call this function."));
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_b()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_b(): only a leader can call this function."));

        // messenger is not yet connected, it's not ready
        //
        CATCH_REQUIRE_FALSE(lock->is_daemon_ready());

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/cluck_daemon_test_three_computers.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (3 cluckd): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()
}


CATCH_TEST_CASE("cluck_daemon_ten_computers", "[cluckd][daemon]")
{
    CATCH_START_SECTION("cluck_daemon_ten_computers: verify cluckd")
    {
        addr::addr a(get_address());

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--candidate-priority",
            "5",
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        // no elections happened, 'lock' is not a leader
        //
        CATCH_REQUIRE(lock->is_leader() == nullptr);
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_a()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_a(): only a leader can call this function."));
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_b()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_b(): only a leader can call this function."));

        // messenger is not yet connected, it's not ready
        //
        CATCH_REQUIRE_FALSE(lock->is_daemon_ready());

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/cluck_daemon_test_ten_computers.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (10 cluckd): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()
}


CATCH_TEST_CASE("cluck_daemon_specialized_tests", "[cluckd][daemon]")
{
    CATCH_START_SECTION("cluck_daemon_specialized_tests: verify early LOCK_LEADERS")
    {
        addr::addr a(get_address());

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        // no elections happened, 'lock' is not a leader
        //
        CATCH_REQUIRE(lock->is_leader() == nullptr);
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_a()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_a(): only a leader can call this function."));
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_b()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_b(): only a leader can call this function."));

        // messenger is not yet connected, it's not ready
        //
        CATCH_REQUIRE_FALSE(lock->is_daemon_ready());

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/cluck_daemon_test_early_lock_leaders.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (3 cluckd): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("cluck_daemon_specialized_tests: verify forwarded (off)")
    {
        addr::addr a(get_address());

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--candidate-priority",
            "off",
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        // no elections happened, 'lock' is not a leader
        //
        CATCH_REQUIRE(lock->is_leader() == nullptr);
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_a()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_a(): only a leader can call this function."));
        CATCH_REQUIRE_THROWS_MATCHES(
              lock->get_leader_b()
            , cluck::logic_error
            , Catch::Matchers::ExceptionMessage("logic_error: cluckd::get_leader_b(): only a leader can call this function."));

        // messenger is not yet connected, it's not ready
        //
        CATCH_REQUIRE_FALSE(lock->is_daemon_ready());

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/cluck_daemon_test_forwarder.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (3 cluckd): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()
}


CATCH_TEST_CASE("cluck_daemon_failures", "[cluckd][daemon][fail]")
{
    CATCH_START_SECTION("cluck_daemon_failures: entering too slowly")
    {
        addr::addr a(get_address());

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/failed_with_timing_out_entering.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (entering timing out): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("cluck_daemon_failures: too many locks")
    {
        addr::addr a(get_address());

        cluck::timeout_t const default_obtention_timeout(cluck::get_lock_obtention_timeout());
        cluck::set_lock_obtention_timeout(cluck::timeout_t(120, 0));

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--candidate-priority",
            "10",
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/failed_with_too_many_locks.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::variable_integer::pointer_t var(
                std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::variable_integer>(
                          "cluck_maximum_entering_locks"));
        var->set_integer(cluck::CLUCK_MAXIMUM_ENTERING_LOCKS);
        s->set_variable(var);
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (entering timing out): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);

        cluck::set_lock_obtention_timeout(default_obtention_timeout);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("cluck_daemon_failures: send a DROP_TICKET to cancel an entering LOCK")
    {
        addr::addr a(get_address());

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--candidate-priority",
            "10",
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",

            "--server-name",
            snapdev::gethostname(),
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/drop_entering_ticket.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (get max ticket overflow): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()
}


CATCH_TEST_CASE("cluck_daemon_errors", "[cluckd][daemon][error]")
{
    CATCH_START_SECTION("cluck_daemon_errors: invalid logger parameter")
    {
        SNAP_CATCH2_NAMESPACE::reset_log_errors safe_counters;
        char const * const args[] = {
            "cluckd", // name of command
            "--log-severity",
            "unknown-severity-name",
            nullptr
        };

        CATCH_REQUIRE_THROWS_MATCHES(
              std::make_shared<cluck_daemon::cluckd>(3, const_cast<char **>(args))
            , advgetopt::getopt_exit
            , Catch::Matchers::ExceptionMessage("getopt_exception: logger options generated an error."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("cluck_daemon_errors: standalone command line options are not allowed")
    {
        SNAP_CATCH2_NAMESPACE::reset_log_errors safe_counters;
        char const * const args[] = {
            "cluckd", // name of command
            "filename",
            nullptr
        };

        CATCH_REQUIRE_THROWS_MATCHES(
              std::make_shared<cluck_daemon::cluckd>(2, const_cast<char **>(args))
            , advgetopt::getopt_exit
            , Catch::Matchers::ExceptionMessage("getopt_exception: errors were found on your command line, environment variable, or configuration file."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("cluck_daemon_errors: double severity")
    {
        SNAP_CATCH2_NAMESPACE::reset_log_errors safe_counters;
        char const * const args[] = {
            "cluckd", // name of command
            "--log-severity",
            "INFO",
            "ERROR",        // oops, the log severity level "leaked"
            nullptr
        };

        CATCH_REQUIRE_THROWS_MATCHES(
              std::make_shared<cluck_daemon::cluckd>(4, const_cast<char **>(args))
            , advgetopt::getopt_exit
            , Catch::Matchers::ExceptionMessage("getopt_exception: errors were found on your command line, environment variable, or configuration file."));
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("cluck_daemon_errors: get max ticket overflow")
    {
        addr::addr a(get_address());

        std::vector<std::string> const args = {
            "cluckd", // name of command
            "--candidate-priority",
            "10",
            "--communicatord-listen",
            "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
            "--path-to-message-definitions",

            // WARNING: the order matters, we want to test with our source
            //          (i.e. original) files first
            //
            SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",
        };

        // convert arguments
        //
        std::vector<char const *> args_strings;
        args_strings.reserve(args.size() + 1);
        for(auto const & arg : args)
        {
            args_strings.push_back(arg.c_str());
        }
        args_strings.push_back(nullptr); // NULL terminated

        cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
        lock->add_connections();

        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/failed_with_max_ticket_too_large.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        CATCH_REQUIRE(l != nullptr);
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();

        e->set_thread_done_callback([lock]()
            {
                lock->stop(true);
            });

        try
        {
            lock->run();
        }
        catch(cluck::out_of_range const & ex)
        {
            SNAP_LOG_INFO
                << "got the expected exception: "
                << ex
                << SNAP_LOG_SEND;

            std::string const expected("out_of_range: ticket::max_ticket() tried to generate the next ticket and got a wrapping around number.");
            CATCH_REQUIRE(ex.what() == expected);

            // the communicator daemon may still have connections because of
            // the exception
            //
            ed::connection::vector_t connections(ed::communicator::instance()->get_connections());
            for(auto c : connections)
            {
                ed::communicator::instance()->remove_connection(c);
            }
        }
        catch(std::exception const & ex)
        {
            SNAP_LOG_FATAL
                << "an exception occurred while running cluckd (get max ticket overflow): "
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            if(b != nullptr) for(auto const & line : b->get_stack_trace())
            {
                SNAP_LOG_FATAL
                    << "    "
                    << line
                    << SNAP_LOG_SEND;
            }

            throw;
        }

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("cluck_daemon_errors: too many computers are off")
    {
        addr::addr a(get_address());

        for(int version(1); version <= 4; ++version)
        {
            for(int off_mask(1); off_mask < (version == 4 ? 8 : 2); ++off_mask)
            {
                std::string diagnostic_msg("version: " + std::to_string(version));
                if(version == 4)
                {
                    diagnostic_msg += '/';
                    diagnostic_msg += std::to_string(off_mask);
                }
                ::snaplogger::nested_diagnostic version_diagnostic(diagnostic_msg, true);

                std::vector<std::string> const args = {
                    "cluckd", // name of command
                    "--candidate-priority",
                    (version == 2 || (version == 4 && (off_mask & 0x04) != 0) ? "15" : "10"),
                    "--communicatord-listen",
                    "cd://" + a.to_ipv4or6_string(addr::STRING_IP_ADDRESS_PORT),
                    "--path-to-message-definitions",

                    // WARNING: the order matters, we want to test with our source
                    //          (i.e. original) files first
                    //
                    SNAP_CATCH2_NAMESPACE::g_source_dir() + "/daemon/message-definitions:"
                        + SNAP_CATCH2_NAMESPACE::g_dist_dir() + "/share/eventdispatcher/messages",
                };

                // convert arguments
                //
                std::vector<char const *> args_strings;
                args_strings.reserve(args.size() + 1);
                for(auto const & arg : args)
                {
                    args_strings.push_back(arg.c_str());
                }
                args_strings.push_back(nullptr); // NULL terminated

                cluck_daemon::cluckd::pointer_t lock(std::make_shared<cluck_daemon::cluckd>(args.size(), const_cast<char **>(args_strings.data())));
                lock->add_connections();

                std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
                std::string const filename(source_dir + "/tests/rprtr/failed_with_too_many_off_v" + std::to_string(version) + ".rprtr");
                SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
                CATCH_REQUIRE(l != nullptr);
                SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
                if(version == 4)
                {
                    SNAP_CATCH2_NAMESPACE::reporter::variable_integer::pointer_t var(
                            std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::variable_integer>(
                                      "off_mask"));
                    var->set_integer(off_mask);
                    s->set_variable(var);
                }
                SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
                p->parse_program();

                SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
                e->start();

                e->set_thread_done_callback([lock]()
                    {
                        lock->stop(true);
                    });

                try
                {
                    lock->run();
                }
                catch(std::exception const & ex)
                {
                    SNAP_LOG_FATAL
                        << "an exception occurred while running cluckd (get max ticket overflow): "
                        << ex
                        << SNAP_LOG_SEND;

                    libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
                    if(b != nullptr) for(auto const & line : b->get_stack_trace())
                    {
                        SNAP_LOG_FATAL
                            << "    "
                            << line
                            << SNAP_LOG_SEND;
                    }

                    throw;
                }

                CATCH_REQUIRE(s->get_exit_code() == 0);
            }
        }
    }
    CATCH_END_SECTION()
}



// vim: ts=4 sw=4 et
