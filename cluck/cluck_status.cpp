// Copyright (c) 2016-2025  Made to Order Software Corp.  All Rights Reserved
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
#include    "cluck/cluck_status.h"

#include    "cluck/exception.h"
#include    "cluck/names.h"


// eventdispatcher
//
//#include    <eventdispatcher/names.h>


// communicatord
//
//#include    <communicatord/communicator.h>
#include    <communicatord/names.h>


// cppthread
//
//#include    <cppthread/guard.h>
//#include    <cppthread/mutex.h>
//#include    <cppthread/thread.h>


// snapdev
//
//#include    <snapdev/not_reached.h>


// last include
//
#include    <snapdev/poison.h>



/** \file
 * \brief Implements a the cluck status signal.
 *
 * When the cluck daemon is ready to receive LOCK messages, it sends a
 * LOCK_READY to the local services. Until that message is received by
 * clients, the cluck daemon will be unresponsive. This means trying
 * to obtain a lock may just time out.
 *
 * The cluck status functions allow you to get notified once the lock
 * system is ready. It does the following:
 *
 * * on initialization, it sends a LOCK_STATUS
 * * listens for LOCK_READY messages
 * * listens for NO_LOCK messages
 *
 * When it receives a LOCK_READY, it marks the cluck status as valid,
 * ready to receive LOCK messages.
 *
 * When it receives a NO_LOCK, it marks the cluck status as invalid,
 * attempting to obtain a LOCK may result in a timeout because the
 * cluck daemon is not ready.
 *
 * Usage:
 *
 * To initialize the lock status listener, call listen_to_cluck_status().
 * It expects a pointer to your messenger, dispatcher, and a callback.
 * The callback gets called each time the status changes.
 *
 * To know the current status, call the is_lock_ready() function. If it
 * returns false, then the cluck daemon is not yet ready. If it returns
 * true, a LOCK will work as expected.
 */



namespace cluck
{

namespace
{



/** \brief Record the current status of the cluck daemon.
 *
 * The cluck deamon always makes sure that it is up and running. If something
 * goes awry (i.e. a leader computer goes down), then it sends a `NO_LOCK`
 * message to all the local services.
 *
 * When everything is ready to send `LOCK` messages and actually obtain such
 * locks, the cluck daemon sends a `LOCK_READY` message.
 *
 * The g_lock_ready variable is set to `true` when the client receives the
 * `LOCK_READY` and to `false` when the client receives `NO_LOCK`. This
 * allows us to have the is_lock_ready() function returning the correct
 * status of the cluck daemon.
 *
 * \todo
 * We probably need to setup this flag behind a guard (mutex lock/unlock
 * when reading/writing this value).
 */
bool g_lock_ready = false;


/** \brief Setup the lock status.
 *
 * This callback function is called whenever the status of the local
 * cluck daemon changes from accepting to not accepting locks.
 *
 * The function updates the g_lock_ready flag according to the command
 * found in the \p msg parameter.
 *
 * The \p callback, if not null, gets called with the \p msg parameter.
 * This gives the user the ability to react immediately when the status
 * of the cluck daemon changes. In some cases, the reaction is to mark
 * the client service as itself being ready. The current status can
 * be checked from within the callback by calling the is_lock_ready()
 * function.
 *
 * \param[in] msg  The message being processed.
 * \param[in] callback  The user callback.
 *
 * \sa is_lock_ready()
 * \sa listen_to_cluck_status()
 */
void msg_lock_status(
      ed::message & msg
    , typename ed::dispatcher_match::execute_callback_t callback)
{
    g_lock_ready = msg.get_command() == g_name_cluck_cmd_lock_ready;

    if(callback != nullptr)
    {
        callback(msg);
    }
}



}
// no name namespace






/** \brief Start listening to changes in the cluck status.
 *
 * This function adds two commands to the \p dispatcher:
 *
 * \li LOCK_READY -- the cluck daemon is ready to receive LOCK messages
 * \li NO_LOCK -- the cluck daemon is no ready, LOCK messages will be
 * cached and may timeout before the daemon becomes ready
 *
 * When called, the function also emits a LOCK_STATUS command to the
 * locak cluck daemon. This ensures that we receive a status from the
 * cluck daemon (i.e. if we missed the last change, it could take a
 * very long time before the status of the cluck daemon changes again).
 *
 * \param[in] messenger  Your messenger to send and receive commands.
 * \param[in] dispatcher  Your dispatcher to list for LOCK_READY and NO_LOCK
 * messages.
 * \param[in] callback  A callback that gets called each time the cluck
 * daemon status changes.
 */
void listen_to_cluck_status(
      ed::connection_with_send_message::pointer_t messenger
    , ed::dispatcher::pointer_t dispatcher
    , typename ed::dispatcher_match::execute_callback_t callback)
{
    {
        ed::dispatcher_match lock_ready(ed::define_match(
                  ed::Expression(g_name_cluck_cmd_lock_ready)
                , ed::Callback(std::bind(&msg_lock_status, std::placeholders::_1, callback))));
        dispatcher->add_match(lock_ready);
    }

    {
        ed::dispatcher_match lock_ready(ed::define_match(
                  ed::Expression(g_name_cluck_cmd_no_lock)
                , ed::Callback(std::bind(&msg_lock_status, std::placeholders::_1, callback))));
        dispatcher->add_match(lock_ready);
    }

    ed::message lock_status_msg;
    lock_status_msg.set_command(g_name_cluck_cmd_lock_status);
    lock_status_msg.add_parameter(communicatord::g_name_communicatord_param_cache, communicatord::g_name_communicatord_value_no);
    messenger->send_message(lock_status_msg);
}


/** \brief Check the current status of the cluck daemon.
 *
 * This function returns true if the cluck daemon is up and ready to receive
 * LOCK messages. The function returns false if the cluck daemon is not yet
 * ready.
 *
 * Note that sending a LOCK message to a cluck daemon which is not ready
 * results in the LOCK message getting cached. If the cluck daemon does
 * not get ready before the lock times out, then a LOCK_FAILED reply is
 * sent to the client.
 *
 * A better way is for the client to wait for this flag to become true
 * before requesting a LOCK. In effect, the initialization process of
 * your service should take this flag in account. Until the callback
 * passed to the listen_to_cluck_status() function is called and
 * the is_lock_ready() function returns true, your own service is not
 * quite fully ready.
 *
 * \note
 * It is possible that the cluck daemon stops being ready. This happens
 * if 2 or more leaders go down and your local system losses its connections
 * to remote communicator daemons. In particular, this happens when a system
 * is about to be rebooted or the network goes down.
 *
 * \return true if the LOCK_READY message was last received.
 */
bool is_lock_ready()
{
    return g_lock_ready;
}



} // namespace cluck
// vim: ts=4 sw=4 et
