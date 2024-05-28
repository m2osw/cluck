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
//#include    <cluck/exception.h>
//#include    <cluck/names.h>
//#include    <cluck/version.h>


// eventdispatcher
//
//#include    <eventdispatcher/communicator.h>
//#include    <eventdispatcher/dispatcher.h>
//#include    <eventdispatcher/names.h>
//#include    <eventdispatcher/tcp_client_permanent_message_connection.h>

#include    <eventdispatcher/reporter/executor.h>
//#include    <eventdispatcher/reporter/lexer.h>
#include    <eventdispatcher/reporter/parser.h>
//#include    <eventdispatcher/reporter/state.h>


// advgetopt
//
//#include    <advgetopt/utils.h>


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


//class test_timer
//    : public ed::timer
//{
//public:
//    typedef std::shared_ptr<test_timer> pointer_t;
//
//    test_timer(test_messenger::pointer_t m)
//        : timer(-1)
//        , f_messenger(m)
//    {
//        set_name("test_timer");
//    }
//
//    void process_timeout()
//    {
//        remove_from_communicator();
//        f_messenger->remove_from_communicator();
//        f_timed_out = true;
//    }
//
//    bool timed_out_prima() const
//    {
//        return f_timed_out;
//    }
//
//private:
//    test_messenger::pointer_t   f_messenger = test_messenger::pointer_t();
//    bool                        f_timed_out = false;
//};



} // no name namespace



CATCH_TEST_CASE("cluck_daemon", "[cluckd][daemon]")
{
    CATCH_START_SECTION("cluck_daemon: verify cluckd")
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

        // convert arguments so we can use them with execvpe()
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
        std::string const filename(source_dir + "/tests/rprtr/cluck_daemon_test.rprtr");
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
                << "an exception occurred while running cluckd:"
                << ex
                << SNAP_LOG_SEND;

            libexcept::exception_base_t const * b(dynamic_cast<libexcept::exception_base_t const *>(&ex));
            for(auto const & line : b->get_stack_trace())
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



// vim: ts=4 sw=4 et
