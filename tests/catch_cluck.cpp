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


// cluck
//
#include    <cluck/names.h>
#include    <cluck/version.h>


// eventdispatcher
//
#include    <eventdispatcher/communicator.h>
#include    <eventdispatcher/dispatcher.h>
#include    <eventdispatcher/names.h>
#include    <eventdispatcher/tcp_client_permanent_message_connection.h>

#include    <eventdispatcher/reporter/executor.h>
#include    <eventdispatcher/reporter/lexer.h>
#include    <eventdispatcher/reporter/parser.h>
#include    <eventdispatcher/reporter/state.h>


// last include
//
#include    <snapdev/poison.h>


namespace
{



// the cluck class requires a messenger to function, it is a client
// extension instead of a standalone client
//
class messenger
    : public ed::tcp_client_permanent_message_connection
{
public:
    typedef std::shared_ptr<messenger> pointer_t;

    enum class sequence_t
    {
        SEQUENCE_SUCCESS,
        SEQUENCE_FAIL_BAD_PARAMETERS,
        SEQUENCE_FAIL_TIMEOUT_LOCK,
        SEQUENCE_FAIL_TIMEOUT_UNLOCK,
    };

    messenger(
              addr::addr const & a
            , ed::mode_t mode
            , sequence_t sequence)
        : tcp_client_permanent_message_connection(
              a
            , mode
            , ed::DEFAULT_PAUSE_BEFORE_RECONNECTING
            , true
            , "client")  // service name
        , f_sequence(sequence)
        , f_dispatcher(std::make_shared<ed::dispatcher>(this))
    {
        set_name("messenger");    // connection name
        set_dispatcher(f_dispatcher);
        f_dispatcher->add_matches({
            ed::define_match(
                  ed::Expression(cluck::g_name_cluck_cmd_lock)
                , ed::Callback(std::bind(&messenger::msg_lock, this, std::placeholders::_1))
            ),
        });

        // further dispatcher initialization
        //
#ifdef _DEBUG
        f_dispatcher->set_trace();
        f_dispatcher->set_show_matches();
#endif
    }

    virtual void process_connected() override
    {
        // always register at the time we connect
        //
        tcp_client_permanent_message_connection::process_connected();
    }

    void msg_lock(ed::message & msg)
    {
        snapdev::NOT_USED(msg);

        switch(f_sequence)
        {
        case sequence_t::SEQUENCE_SUCCESS:
            {
                ed::message locked;
                locked.reply_to(msg);
                locked.set_command("LOCK");
                locked.add_parameter("param1", 7209);
                if(!send_message(locked, false))
                {
                    throw std::runtime_error("could not send LOCK message");
                }
            }
            break;

        default:
            break;

        }
    }

    //virtual void process_message(ed::message & msg) override
    //{
    //    ++f_step;
    //    std::cout
    //        << "--- \"client\" message ("
    //        << f_step
    //        << "): "
    //        << msg
    //        << std::endl;

    //    bool disconnect_all(false);

    //    std::string const command(msg.get_command());
    //    if(command == "LOCK")
    //    {
    //    }
    //}

    void disconnect()
    {
        remove_from_communicator();

        ed::connection::pointer_t timer_ptr(f_timer.lock());
        if(timer_ptr != nullptr)
        {
            timer_ptr->remove_from_communicator();
        }
    }

    void set_timer(ed::connection::pointer_t done_timer)
    {
        f_timer = done_timer;
    }

private:
    // the sequence & step define the next action
    //
    sequence_t                  f_sequence = sequence_t::SEQUENCE_SUCCESS;
    ed::dispatcher::pointer_t   f_dispatcher = ed::dispatcher::pointer_t();
    int                         f_step = 0;
    ed::connection::weak_pointer_t
                                f_timer = ed::connection::weak_pointer_t();
};



} // no name namespace



CATCH_TEST_CASE("cluck_client", "[cluck][client]")
{
    CATCH_START_SECTION("successful LOCK")
    {
        std::string const source_dir(SNAP_CATCH2_NAMESPACE::g_source_dir());
        std::string const filename(source_dir + "/tests/rprtr/successful_lock.rprtr");
        SNAP_CATCH2_NAMESPACE::reporter::lexer::pointer_t l(SNAP_CATCH2_NAMESPACE::reporter::create_lexer(filename));
        SNAP_CATCH2_NAMESPACE::reporter::state::pointer_t s(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::state>());
        SNAP_CATCH2_NAMESPACE::reporter::parser::pointer_t p(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::parser>(l, s));
        p->parse_program();

        SNAP_CATCH2_NAMESPACE::reporter::executor::pointer_t e(std::make_shared<SNAP_CATCH2_NAMESPACE::reporter::executor>(s));
        e->start();
        e->run();

        CATCH_REQUIRE(s->get_exit_code() == 0);
    }
    CATCH_END_SECTION()
}


// vim: ts=4 sw=4 et
