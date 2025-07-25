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
#include    "cluck/cluck.h"

#include    "cluck/exception.h"
#include    "cluck/names.h"


// eventdispatcher
//
#include    <eventdispatcher/names.h>


// communicatord
//
#include    <communicatord/communicator.h>
#include    <communicatord/names.h>


// cppthread
//
#include    <cppthread/guard.h>
#include    <cppthread/mutex.h>
#include    <cppthread/thread.h>


// snapdev
//
#include    <snapdev/not_reached.h>


// last include
//
#include    <snapdev/poison.h>



/** \file
 * \brief Implementation of the cluck class.
 *
 * A cluck object is used to obtain a cluster lock.
 *
 * This allows programs to run code in a cluster safe manner. In other words,
 * it ensures that a single instance of a function runs in an entire cluster
 * of computers. This can also be useful as a lock between processes running
 * on a single computer, although in that case an flock(2) call is likely
 * much more efficient (if you can guarantee that all said processes will
 * be running on the same computer). See the snapdev/lockfile.h for such a
 * lock object.
 *
 * The implementation creates a LOCK message and sends it to the cluck
 * service. The service then either immediately replies with a LOCKED
 * message or waits for the lock to be released by another program.
 * Once done with a lock, a program sends the UNLOCK message to release
 * the lock.
 *
 * The process can timeout or fail, in which case the lock_failed()
 * function gets called instead of the lock_obtained() one. It is also
 * possible to run common code after the lock is released by implementing
 * the finally() function. By default, those functions call your callbacks,
 * which can be much more practical than deriving this class.
 *
 * Note that the cluck class is a communicator connection of type ed::timer.
 * You are responsible for adding that class to the communicator.
 *
 * You must have a messenger object with a dispatcher to create a cluck
 * object. The name passed to the cluck constructor is the name of the
 * lock. Different names create different locks. In other words, a lock
 * named A does not prevent a lock named B from being obtained.
 */

namespace cluck
{



namespace
{



cppthread::mutex            g_mutex = cppthread::mutex();
ed::dispatcher_match::tag_t g_tag = ed::dispatcher_match::tag_t();
cluck::serial_t             g_serial = cluck::serial_t();
timeout_t                   g_lock_obtention_timeout = CLUCK_LOCK_OBTENTION_DEFAULT_TIMEOUT;
timeout_t                   g_lock_duration_timeout = CLUCK_LOCK_DURATION_DEFAULT_TIMEOUT;
timeout_t                   g_unlock_timeout = CLUCK_UNLOCK_DEFAULT_TIMEOUT;


ed::match_t match_command_and_tag(ed::dispatcher_match const * m, ed::message & msg)
{
    if(m->f_expr != nullptr
    && m->f_expr == msg.get_command()
    && msg.has_parameter(g_name_cluck_param_tag)
    && msg.get_integer_parameter(g_name_cluck_param_tag) == m->f_tag)
    {
        return ed::match_t::MATCH_TRUE;
    }

    return ed::match_t::MATCH_FALSE;
}


ed::dispatcher_match::tag_t get_next_tag()
{
    cppthread::guard lock(g_mutex);
    ++g_tag;
    if(g_tag == ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG)
    {
        g_tag = 1; // LCOV_EXCL_LINE
    }
    return g_tag;
}


cluck::serial_t get_next_serial()
{
    cppthread::guard lock(g_mutex);
    ++g_serial;
    if(g_serial == 0)
    {
        g_serial = 1; // LCOV_EXCL_LINE
    }
    return g_serial;
}



} // no name namespace



timeout_t get_lock_obtention_timeout()
{
    cppthread::guard lock(g_mutex);
    return g_lock_obtention_timeout;
}


void set_lock_obtention_timeout(timeout_t timeout)
{
    if(timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        timeout = CLUCK_LOCK_OBTENTION_DEFAULT_TIMEOUT;
    }
    else
    {
        timeout = std::clamp(timeout, CLUCK_MINIMUM_TIMEOUT, CLUCK_LOCK_OBTENTION_MAXIMUM_TIMEOUT);
    }

    cppthread::guard lock(g_mutex);
    g_lock_obtention_timeout = timeout;
}


timeout_t get_lock_duration_timeout()
{
    cppthread::guard lock(g_mutex);
    return g_lock_duration_timeout;
}


void set_lock_duration_timeout(timeout_t timeout)
{
    if(timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        timeout = CLUCK_LOCK_DURATION_DEFAULT_TIMEOUT;
    }
    else
    {
        timeout = std::clamp(timeout, CLUCK_MINIMUM_TIMEOUT, CLUCK_MAXIMUM_TIMEOUT);
    }

    cppthread::guard lock(g_mutex);
    g_lock_duration_timeout = timeout;
}


timeout_t get_unlock_timeout()
{
    cppthread::guard lock(g_mutex);
    return g_unlock_timeout;
}


void set_unlock_timeout(timeout_t timeout)
{
    if(timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        timeout = CLUCK_UNLOCK_DEFAULT_TIMEOUT;
    }
    else
    {
        timeout = std::clamp(timeout, CLUCK_UNLOCK_MINIMUM_TIMEOUT, CLUCK_MAXIMUM_TIMEOUT);
    }

    cppthread::guard lock(g_mutex);
    g_unlock_timeout = timeout;
}


/** \brief Create a cluster lock.
 *
 * The cluck object expects at least three parameters to offer the ability
 * to create a cluster lock. A cluster lock allows you to run one or more
 * functions in a cluster safe manner (i.e. by a single process running in
 * a computer cluster of any size).
 *
 * First, the function expects a \p object_name parameter. This is the name
 * of the lock. To run a function within a single process, any number of
 * processes can request a lock using the exact same \p object_name.
 *
 * Second, the cluck object needs to send messages and for the purposes needs
 * to have access to a \p connection that has the send_message()
 * functionality implemented. In most likelihood, this is your communicatord
 * derived class (your messenger).
 *
 * Third, the function has to react to replies from the messages it sends.
 * This is accomplished by the \p dispatcher object. Again, this is likely
 * your messenger dispatcher.
 *
 * The constructor collects that data. The lock() function sets up the
 * \p dispatcher callbacks and sends a LOCK message to the \p connection.
 * The unlock() function is implemented as a failure, your error callbacks
 * are called and then the finally callbacks, which removes the
 * \p dispatcher callbacks. (Note: there are two sets of callbacks, the
 * dispatcher callbacks are automatically managed, the success, error,
 * and finally callbacks are managed by you, the cluck user.)
 *
 * Many functions cannot be called once the lock() function was called,
 * including the lock() function itself. These work again after the
 * finally() function was called. This is done this way because some of
 * the cluck parameters cannot or should not be changed while a lock is
 * being processed.
 *
 * The \p dispatcher expects to receive one of the LOCKED or LOCKFAILED
 * messages after the LOCK was sent to the cluck service. Once done with
 * a lock, we send an UNLOCK message and in that case we expected the
 * UNLOCKED message as a reply. If a processes uses a LOCK for too long
 * (i.e. the cluck service notices that the lock timed out), it may also
 * receive the UNLOCKING message letting it know that the lock is about
 * to be terminated. Your cluck objects react to that message by stopping
 * the lock and you are expected to not run any additional cluster safe
 * code since it may become unsafe at any moment.
 *
 * Keep in mind that the object is used 100% asynchroneously. If you want
 * to execute code after the lock was released, make sure to also define
 * a set of finally callbacks.
 *
 * If the code you need to execute requires sending and receiving other
 * messages (i.e. go back in the communicator run() loop), then it is
 * required to use the mode_t::CLUCK_MODE_EXTENDED mode. Otherwise, the
 * lock gets released as soon as all your lock obtained callbacks return.
 *
 * \code
 *     func_start()
 *     {
 *         // code before lock...
 *
 *         cluck::pointer_t my_lock(std::make_shared<cluck>("my-lock", f_messenger, f_messenger);
 *         f_communicator->add_connection(my_lock);
 *         my_lock->add_lock_obtained_callback(func_success);
 *         my_lock->add_lock_failed_callback(func_failure);
 *         my_lock->add_finally_callback(func_continue);
 *         my_lock->lock();
 *
 *         // returns to run() loop
 *     }
 *
 *     // on success
 *     bool func_success()
 *     {
 *         // here is the cluster safe code
 *         ...code run on success only...
 *     }
 *
 *     // on failure
 *     bool func_failure()
 *     {
 *         ...code run on failure only...
 *     }
 *
 *     // after func_success() or func_failure()
 *     bool func_finally()
 *     {
 *         ...continue here after the lock was released...
 *
 *         // returns to run() loop
 *     }
 * \endcode
 *
 * \note
 * The f_communicator->add_connection() call is what holds the connection.
 * You may also keep a copy of the pointer if necessary. If you need the
 * connection just once, the finally function can be used to remove the
 * connection from the communicator.
 *
 * The cluck object uses default time out durations. These can be changed
 * using the global functions provided for the purpose. It is also possible
 * to change those values on a per cluck object before calling the
 * lock() function.
 *
 * The timer is disabled by default. Various functions enable it and set
 * a timeout date to be able to track timeouts locally since we cannot
 * be sure that the cluck service is even running at the time a cluck
 * object tries to communicate with it. Please, do not attempt to use
 * this timer for anything else as it would likely cause issues with this
 * object implementation.
 *
 * \note
 * The constructor initializes the timeouts to the special value
 * CLUCK_DEFAULT_TIMEOUT. In that case, the corresponding global durations
 * are used at the time the corresponding timeout is necessary.
 *
 * The following shows the preparation and demantlement of your client
 * and the successful path of a cluck lock:
 *
 * \msc
 *    client,communicatord,cluckd;
 *
 *    client->communicatord [label="REGISTER"];
 *    communicatord->client [label="READY"];
 *    communicatord->client [label="HELP"];
 *    client->communicatord [label="COMMANDS"];
 *    ...;
 *    client->communicatord [label="LOCK"];
 *    communicatord->cluckd [label="LOCK"];
 *    cluckd->communicatord [label="LOCKED"];
 *    communicatord->client [label="LOCKED"];
 *    client=>client [label="lock_obtained_callbacks"];
 *    client=>client [label="finally_callbacks"];
 *    ...;
 *    client->communicatord [label="UNLOCK"];
 *    communicatord->cluckd [label="UNLOCK"];
 *    cluckd->communicatord [label="UNLOCKED"];
 *    communicatord->client [label="UNLOCKED"];
 *    ...;
 *    client->communicatord [label="UNREGISTER"];
 * \endmsc
 *
 * \note
 * If you use a simple lock, then the finally_callbacks are called after
 * your client send an UNLOCK but before it received the UNLOCKED message.
 *
 * The following shows an unsuccessful lock:
 *
 * \msc
 *    client,communicatord,cluckd;
 *
 *    client->communicatord [label="REGISTER"];
 *    communicatord->client [label="READY"];
 *    communicatord->client [label="HELP"];
 *    client->communicatord [label="COMMANDS"];
 *    ...;
 *    client->communicatord [label="LOCK"];
 *    communicatord->cluckd [label="LOCK"];
 *    cluckd->communicatord [label="LOCK_FAILED"];
 *    communicatord->client [label="LOCK_FAILED"];
 *    client=>client [label="lock_failed_callbacks"];
 *    client=>client [label="finally_callbacks"];
 *    ...;
 *    client->communicatord [label="UNREGISTER"];
 * \endmsc
 *
 * \param[in] object_name  The name of the lock.
 * \param[in] connection  The connection used to send messages.
 * \param[in] dispatcher  The dispatcher used to receive messages.
 * \param[in] mode  Defines the usage of the lock.
 */
cluck::cluck(
          std::string const & object_name
        , ed::connection_with_send_message::pointer_t messenger
        , ed::dispatcher::pointer_t dispatcher
        , mode_t mode)
    : timer(0)
    , f_object_name(object_name)
    , f_tag(get_next_tag())
    , f_connection(messenger)
    , f_dispatcher(dispatcher)
    , f_mode(mode)
    , f_lock_obtention_timeout(CLUCK_DEFAULT_TIMEOUT)
    , f_lock_duration_timeout(CLUCK_DEFAULT_TIMEOUT)
    , f_unlock_timeout(CLUCK_DEFAULT_TIMEOUT)
{
    if(messenger == nullptr
    || dispatcher == nullptr)
    {
        throw invalid_parameter("messenger & dispatcher parameters must be defined in cluck::cluck() constructor.");
    }

    set_enable(false);
    set_name("cluck::" + object_name);

    f_connection->add_help_callback(std::bind(&cluck::help, this, std::placeholders::_1));
}


/** \brief Make sure to clean up the dispatcher.
 *
 * If the lock() command was called, then a set of dispatcher functions
 * were added. The destructor makes sure those get removed in case the
 * finally() function does not get called first.
 *
 * \sa finally()
 */
cluck::~cluck()
{
    f_dispatcher->remove_matches(f_tag);
}


/** \brief Add a callback function to call when the lock is obtained.
 *
 * This function adds a callback function to the callback manager used
 * to register function called whenever the lock is obtained.
 *
 * \param[in] func  The callback function to call.
 * \param[in] priority  The priority of the callback.
 *
 * \return The callback identifier.
 */
cluck::callback_manager_t::callback_id_t cluck::add_lock_obtained_callback(callback_t func, callback_manager_t::priority_t priority)
{
    return f_lock_obtained_callbacks.add_callback(func, priority);
}


/** \brief Remove a callback function from the lock obtained list.
 *
 * This function is the converse of the add_lock_obtained_callback(). If you
 * save the returned callback identifier, you can later call this function
 * to remove the callback explicitly.
 *
 * \param[in] id  The identifier of the callback to remove.
 *
 * \return true if the callback was indeed removed.
 */
bool cluck::remove_lock_obtained_callback(callback_manager_t::callback_id_t id)
{
    return f_lock_obtained_callbacks.remove_callback(id);
}


/** \brief Add a callback function to call when the lock has failed.
 *
 * This function adds a callback function to the callback manager used
 * to register function called whenever the lock obtention fails.
 *
 * \param[in] func  The callback function to call.
 * \param[in] priority  The priority of the callback.
 *
 * \return The callback identifier.
 */
cluck::callback_manager_t::callback_id_t cluck::add_lock_failed_callback(callback_t func, callback_manager_t::priority_t priority)
{
    return f_lock_failed_callbacks.add_callback(func, priority);
}


/** \brief Remove a callback function from the lock failed list.
 *
 * This function is the converse of the add_lock_failed_callback(). If you
 * save the returned callback identifier, you can later call this function
 * to remove the callback explicitly.
 *
 * \param[in] id  The identifier of the callback to remove.
 *
 * \return true if the callback was indeed removed.
 */
bool cluck::remove_lock_failed_callback(callback_manager_t::callback_id_t id)
{
    return f_lock_failed_callbacks.remove_callback(id);
}


/** \brief Add a callback function to call when done with the lock.
 *
 * This function adds a callback function to the callback manager used
 * to register function called whenever the object is done with the lock.
 *
 * \param[in] func  The callback function to call.
 * \param[in] priority  The priority of the callback.
 *
 * \return The callback identifier.
 */
cluck::callback_manager_t::callback_id_t cluck::add_finally_callback(callback_t func, callback_manager_t::priority_t priority)
{
    return f_finally_callbacks.add_callback(func, priority);
}


/** \brief Remove a callback function from the lock finally list.
 *
 * This function is the converse of the add_finally_callback(). If you
 * save the returned callback identifier, you can later call this function
 * to remove the callback explicitly.
 *
 * \param[in] id  The identifier of the callback to remove.
 *
 * \return true if the callback was indeed removed.
 */
bool cluck::remove_finally_callback(callback_manager_t::callback_id_t id)
{
    return f_finally_callbacks.remove_callback(id);
}


/** \brief Retrieve the current lock obtention duration.
 *
 * This function returns the current timeout for the obtention of a lock.
 * It can be useful if you want to use a lock with a different obtention
 * timeout and then restore the previous value afterward.
 *
 * \return Current lock obtention maximum wait period in microseconds.
 */
timeout_t cluck::get_lock_obtention_timeout() const
{
    return f_lock_obtention_timeout;
}


/** \brief Set how long to wait for an inter-process lock to take.
 *
 * This function lets you change the default amount of time the
 * inter-process locks can wait before forfeiting the obtention
 * of a new lock.
 *
 * This amount can generally remain pretty small. For example,
 * you could say that you want to wait just 1 minute even though
 * the lock you want to get will last 24 hours. This means, within
 * one minute your process is told that the lock cannot be
 * obtained. In other words, you cannot do the work you intended
 * to do. If the lock is released within the 1 minute and you
 * are next on the list, you get the lock and can proceed
 * with the work you intended to do.
 *
 * The default is five seconds which for a front end is already
 * quite enormous.
 *
 * \note
 * Calling this function does not change the timeout connection
 * current duration.
 *
 * \param[in] timeout  The number of microseconds to wait to obtain a lock.
 */
void cluck::set_lock_obtention_timeout(timeout_t timeout)
{
    if(timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        f_lock_obtention_timeout = timeout;
    }
    else
    {
        f_lock_obtention_timeout = std::clamp(timeout, CLUCK_MINIMUM_TIMEOUT, CLUCK_LOCK_OBTENTION_MAXIMUM_TIMEOUT);
    }
}


/** \brief Retrieve the current lock duration.
 *
 * This function returns the current lock timeout. It can be useful if
 * you want to use a lock with a different timeout and then restore
 * the previous value afterward.
 *
 * Although if you have access/control of the lock itself, you may instead
 * want to specify the timeout in the snap_lock constructor directly.
 *
 * \return Current lock TTL in seconds.
 */
timeout_t cluck::get_lock_duration_timeout() const
{
    return f_lock_duration_timeout;
}


/** \brief Set how long inter-process locks last.
 *
 * This function lets you change the default amount of time the
 * inter-process locks last (i.e. their "Time To Live") in seconds.
 *
 * For example, to keep locks for 1 hour, use 3600.
 *
 * This value is used whenever a lock is created with its lock
 * duration set to -1.
 *
 * \param[in] timeout  The total number of seconds locks will last for by default.
 */
void cluck::set_lock_duration_timeout(timeout_t timeout)
{
    if(timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        f_lock_duration_timeout = timeout;
    }
    else
    {
        f_lock_duration_timeout = std::clamp(timeout, CLUCK_MINIMUM_TIMEOUT, CLUCK_MAXIMUM_TIMEOUT);
    }
}


/** \brief Retrieve the current unlock duration.
 *
 * This function returns the current unlock duration. It can be useful
 * if you want to use an unlock with a different timeout and then restore
 * the previous value afterward.
 *
 * \return Current unlock TTL in microseconds.
 */
timeout_t cluck::get_unlock_timeout() const
{
    return f_unlock_timeout;
}


/** \brief Set how long we wait on an inter-process unlock acknowledgement.
 *
 * This function lets you change the default amount of time the
 * inter-process unlock lasts (i.e. their "Time To Survive" after
 * a lock time out) in microseconds.
 *
 * For example, to allow your application to take up to 5 minutes to
 * acknowldege a timed out lock, set this value to 300'000'000.
 *
 * \param[in] timeout  The number of microseconds before timing out on unlock.
 */
void cluck::set_unlock_timeout(timeout_t timeout)
{
    if(timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        f_unlock_timeout = timeout;
    }
    else
    {
        f_unlock_timeout = std::clamp(timeout, CLUCK_UNLOCK_MINIMUM_TIMEOUT, CLUCK_MAXIMUM_TIMEOUT);
    }
}


/** \brief Retrieve the mode.
 *
 * The lock can be created in various modes. This function returns the mode
 * used when creating the cluck object. This mode cannot be changed once
 * the lock was created.
 *
 * \return The mode the lock was created with.
 */
mode_t cluck::get_mode() const
{
    return f_mode;
}


/** \brief Retrieve the lock type.
 *
 * The lock can be given a type changing the behavior of the locking
 * mechanism. Please see the set_type() function for additional details.
 *
 * \return The type of lock this cluck object uses at the moment.
 *
 * \sa set_type()
 */
type_t cluck::get_type() const
{
    return f_type;
}


/** \brief Set the lock type.
 *
 * This function changes the type of lock the lock() function uses. It can
 * only be called if the cluck object is not currently busy (i.e. before
 * the lock() function is called).
 *
 * The supported type of locks are:
 *
 * \li Exclusive Lock (type_t::CLUCK_TYPE_READ_WRITE) -- this type of lock
 * can only be obtained by one single process. This is the default. 
 * \li Shared Lock (type_t::CLUCK_TYPE_READ_ONLY) -- this type of lock can
 * be obtained by any number of processes. It, however, prevents exclusive
 * locks from taking.
 * \li Exclusive Lock with Priority (type_t::CLUCK_TYPE_READ_WRITE_PRIORITY) --
 * this is similar to the basic Exclusive Lock, except that it prevents further
 * Shared Locks from happening, making sure that the exclusive lock happens
 * as soon as possible.
 *
 * \exception busy
 * If the cluck object is currently in use (trying to obtain a lock, has a
 * lock, releasing a lock) then the busy exception is raised.
 *
 * \param[in] type  One of the type_t enumerations.
 *
 * \sa get_type()
 */
void cluck::set_type(type_t type)
{
    if(is_busy())
    {
        throw busy("this cluck object is busy, you cannot change its type at the moment.");
    }

    f_type = type;
}


/** \brief The reason for the last failure.
 *
 * This function returns the reason of the last failure. Internally, we
 * call the set_reason() to change this value.
 *
 * Here are the reason for failure:
 *
 * \li CLUCK_REASON_NONE -- no failure detected yet
 * \li CLUCK_REAON_LOCAL_TIMEOUT -- our cluck timer called process_timeout()
 * \li CLUCK_REAON_REMOTE_TIMEOUT -- lock could not be obtained within
 * the obtain lock timeout amount of time alotted
 * \li CLUCK_REAON_DEADLOCK -- the lock would result in a deadlock so it
 * was not attemped; in this case the programmer needs to do some code
 * changes to avoid this situation
 *
 * \return The reason why the lock failed.
 *
 * \sa set_reason()
 */
reason_t cluck::get_reason() const
{
    return f_reason;
}


/** \brief Change the reason why a lock failed.
 *
 * This function saves the reason for the last failure. This can be useful
 * to know how to proceed next. i.e. failure to obtain a lock may tell your
 * application that waiting another 5 minutes before trying again is a
 * possibility.
 *
 * \note
 * At the moment, this reason is only used for lock failure, not other
 * potential errors.
 *
 * \param[in] reason  The reason why this cluck object failed getting a lock.
 */
void cluck::set_reason(reason_t reason)
{
    f_reason = reason;
}


/** \brief Called whenever the HELP message is received or new messages are added.
 *
 * This function gets called whenever the dispatcher receives the HELP
 * message. It then replies with the COMMANDS message.
 *
 * The function adds the multiple commands that the cluck supports and which
 * use a non-default match function.
 *
 * \param[in] commands  The list of commands to complete.
 *
 * \return Always return true.
 */
bool cluck::help(advgetopt::string_set_t & commands)
{
    commands.insert(g_name_cluck_cmd_locked);
    commands.insert(g_name_cluck_cmd_lock_failed);
    commands.insert(g_name_cluck_cmd_unlocked);
    commands.insert(g_name_cluck_cmd_unlocking);

    return true;
}


/** \brief Attempt a lock.
 *
 * This function attempts a lock. If a lock was already initiated or is in
 * place, the function fails.
 *
 * On a timeline, the lock obtention and lock duration parameters are used
 * as follow:
 *
 * \code
 * Lock Obtention Fails (timeout)
 *
 *      lock obtention
 *   |<---------------->|
 *   |                  |
 * --+------------------+
 *   ^                  ^
 *   |                  +--- if lock not obtained, lock_failed() called
 *   +--- send LOCK
 *
 * Lock Obtention Succeeds -- early release
 *
 *      lock obtention     lock duration                     unlock timeout
 *   |<-----...|<-------------------------------->|<------------>|
 *   |         |                                  |              |
 * --+---------+----------------------------------+--------------+-->
 *   ^        ^       ^         ^
 *   |        |       |         |
 *   |        |       |         +--- done early, send UNLOCK
 *   |        |       |
 *   |        |       +--- do your work safely
 *   |        |
 *   |        +--- LOCKED received, lock_obtained() called
 *   |
 *   +--- send LOCK
 *
 * Lock Obtention Succeeds -- late release
 *
 *      lock obtention     lock duration                     unlock timeout
 *   |<-----...|<-------------------------------->|<------------>|
 *   |         |                                  |              |
 * --+---------+----------------------------------+--------------+-->
 *   ^        ^       ^                           ^   ^          ^
 *   |        |       |                           |   |          |
 *   |        |       |   safe work ends here ----+   |          |
 *   |        |       |  (client receives UNLOCKING)  |          |
 *   |        |       |                               |          |
 *   |        |       +--- do your work safely        |          |
 *   |        |                                       |          |
 *   |        |  dangerous, but work can end here ----+          |
 *   |        |  by sending UNLOCK                               |
 *   |        |                                                  |
 *   |        +--- LOCKED received, lock_obtained() called       |
 *   |                                                           |
 *   +--- send LOCK           too late to do additional work ----+
 *                            (client receives UNLOCKED)
 * \endcode
 *
 * If the lock obtention times out before we receive a LOCKED message
 * back from the cluck service, the process calls your lock_failed()
 * callback functions.
 *
 * If the LOCKED message is received before the lock obtention times out,
 * then your lock_obtained() callback functions get called.
 *
 * The cluck service will send an UNLOCKING message once the lock duration
 * is reached and no UNLOCK was received. At that point, continuing work
 * is still safe, but considered _dangerous_. It is best to try to limit
 * your work so it is complete by the time the lock end date is past.
 *
 * If the cluck service never receives the UNLOCK message, it ends up
 * sending a UNLOCKED message at the time the "unlock timeout" is reached.
 *
 * \note
 * This function does not trigger any calls to your callbacks. These only
 * happen if this function returns true and once replies are received.
 * If the function returns false, then the lock will never happen.
 *
 * \return true if the lock obtention was properly initiated.
 */
bool cluck::lock()
{
    // is lock still busy?
    //
    if(is_busy())
    {
        return false;
    }

    timeout_t obtention_timeout_date(snapdev::now());
    if(f_lock_obtention_timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        // use global timeout by default
        //
        obtention_timeout_date += ::cluck::get_lock_obtention_timeout();
    }
    else
    {
        obtention_timeout_date += f_lock_obtention_timeout;
    }

    f_serial = get_next_serial();

    // send the LOCK message
    //
    ed::message lock_message;
    lock_message.set_command(g_name_cluck_cmd_lock);
    lock_message.set_service(g_name_cluck_service_name);
    lock_message.add_parameter(g_name_cluck_param_object_name, f_object_name);
    lock_message.add_parameter(g_name_cluck_param_tag, static_cast<int>(f_tag));
    lock_message.add_parameter(g_name_cluck_param_pid, cppthread::gettid());
    lock_message.add_parameter(ed::g_name_ed_param_serial, f_serial);
    lock_message.add_parameter(g_name_cluck_param_timeout, obtention_timeout_date);
    communicatord::request_failure(lock_message);
    if(f_lock_duration_timeout != CLUCK_DEFAULT_TIMEOUT)
    {
        lock_message.add_parameter(g_name_cluck_param_duration, f_lock_duration_timeout);
    }
    if(f_unlock_timeout != CLUCK_DEFAULT_TIMEOUT)
    {
        lock_message.add_parameter(g_name_cluck_param_unlock_duration, f_unlock_timeout);
    }
    if(f_type != type_t::CLUCK_TYPE_READ_WRITE)
    {
        lock_message.add_parameter(g_name_cluck_param_type, static_cast<int>(f_type));
    }
    if(!f_connection->send_message(lock_message))
    {
        // LCOV_EXCL_START
        f_state = state_t::CLUCK_STATE_FAILED;
        set_reason(reason_t::CLUCK_REASON_TRANSMISSION_ERROR);
        snapdev::NOT_REACHED_IN_TEST();
        return false;
        // LCOV_EXCL_STOP
    }

    set_timeout_date(obtention_timeout_date);
    set_enable(true);

    set_reason(reason_t::CLUCK_REASON_NONE);
    f_state = state_t::CLUCK_STATE_LOCKING;

    // start listening to our messages
    //
    ed::dispatcher_match locked(ed::define_match(
              ed::Expression(g_name_cluck_cmd_locked)
            , ed::Callback(std::bind(&cluck::msg_locked, this, std::placeholders::_1))
            , ed::MatchFunc(&match_command_and_tag)
            , ed::Tag(f_tag)));
    f_dispatcher->add_match(locked);

    ed::dispatcher_match lock_failed(ed::define_match(
              ed::Expression(g_name_cluck_cmd_lock_failed)
            , ed::Callback(std::bind(&cluck::msg_lock_failed, this, std::placeholders::_1))
            , ed::MatchFunc(&match_command_and_tag)
            , ed::Tag(f_tag)));
    f_dispatcher->add_match(lock_failed);

    ed::dispatcher_match unlocked(ed::define_match(
              ed::Expression(g_name_cluck_cmd_unlocked)
            , ed::Callback(std::bind(&cluck::msg_unlocked, this, std::placeholders::_1))
            , ed::MatchFunc(&match_command_and_tag)
            , ed::Tag(f_tag)));
    f_dispatcher->add_match(unlocked);

    ed::dispatcher_match unlocking(ed::define_match(
              ed::Expression(g_name_cluck_cmd_unlocking)
            , ed::Callback(std::bind(&cluck::msg_unlocking, this, std::placeholders::_1))
            , ed::MatchFunc(&match_command_and_tag)
            , ed::Tag(f_tag)));
    f_dispatcher->add_match(unlocking);

    ed::dispatcher_match transmission_report(ed::define_match(
              ed::Expression(communicatord::g_name_communicatord_cmd_transmission_report)
            , ed::Callback(std::bind(&cluck::msg_transmission_report, this, std::placeholders::_1))
            , ed::MatchFunc(&ed::one_to_one_callback_match)
            , ed::Tag(f_tag)
            , ed::Priority(ed::dispatcher_match::DISPATCHER_MATCH_CALLBACK_PRIORITY)));
    f_dispatcher->add_match(transmission_report);

    // we just added new commands (at least the first time) which we need to
    // share with the communicator deamon (otherwise it won't forward them
    // to us)
    //
    f_connection->send_commands();

    return true;
}


/** \brief Release the inter-process lock.
 *
 * This function releases this inter-process lock at the time it gets called.
 * When everything works as expected, this function should be called before
 * the lock times out.
 *
 * If the lock was not active (i.e. lock() was never called or returned
 * false last time you called it), then nothing happens.
 *
 * This is useful if you keep the same lock object around and want to
 * lock/unlock it at various time. It is actually very important to
 * unlock your locks so other processes can then gain access to the
 * resources that they protect.
 */
void cluck::unlock()
{
    if(f_state != state_t::CLUCK_STATE_LOCKED
    && f_state != state_t::CLUCK_STATE_LOCKING)
    {
        SNAP_LOG_NOTICE
            << "this cluck object is not currently locked."
            << SNAP_LOG_SEND;
        return;
    }

    f_lock_timeout_date = timeout_t();

    // explicitly send the UNLOCK message and then make sure to unregister
    // from snapcommunicator; note that we do not wait for a reply to the
    // UNLOCK message, since to us it does not matter much as long as the
    // message was sent...
    //
    ed::message unlock_message;
    unlock_message.set_command(g_name_cluck_cmd_unlock);
    unlock_message.set_service(g_name_cluck_service_name);
    unlock_message.add_parameter(g_name_cluck_param_object_name, f_object_name);
    unlock_message.add_parameter(g_name_cluck_param_tag, static_cast<int>(f_tag));
    unlock_message.add_parameter(g_name_cluck_param_pid, gettid());
    unlock_message.add_parameter(ed::g_name_ed_param_serial, f_serial);
    if(!f_connection->send_message(unlock_message))
    {
        // LCOV_EXCL_START
        f_state = state_t::CLUCK_STATE_FAILED;
        set_reason(reason_t::CLUCK_REASON_TRANSMISSION_ERROR);
        lock_failed();
        finally();
        snapdev::NOT_REACHED_IN_TEST();
        return;
        // LCOV_EXCL_STOP
    }

    // give the UNLOCK 5 seconds to happen, if it does not happen, we'll
    // set the state to "failed" and still call the finally() callbacks
    //
    timeout_t unlock_timeout_date(snapdev::now());
    unlock_timeout_date += timeout_t(5, 0); // TODO: use correct timeout instead of hard coded 5s
    set_timeout_date(unlock_timeout_date);
    set_enable(true);

    f_state = state_t::CLUCK_STATE_UNLOCKING;
}


/** \brief Get the exact time when the lock times out.
 *
 * This function is used to check when the current lock will be considerd
 * out of date and thus when you should stop doing whatever requires
 * said lock.
 *
 * You can compare the time against snapdev::now() to know whether the
 * lock timed out. Note that the is_locked() function also technically
 * returns the same information.
 *
 * \code
 *      snapdev::timespec_ex const now(snapdev::now());
 *      if(lock.get_timeout_date() > now()) // i.e. equivalent to lock.is_locked()
 *      {
 *          // lock still in place -- compute time left
 *          //
 *          snapdev::timespec_ex const time_left(lock.get_timeout_date() - now());
 *          ...
 *      }
 * \endcode
 *
 * Remember that this exact date was sent to the cluck service but you may
 * have a clock with a one second or so difference between various computers
 * so if the amount is really small (under 2 seconds) you should probably
 * already considered that the lock has timed out.
 *
 * \return The date when this lock will be over or zero if the lock is
 *         not currently active.
 *
 * \sa lock_timedout()
 * \sa is_locked()
 * \sa time_left()
 */
timeout_t cluck::get_timeout_date() const
{
    return f_lock_timeout_date;
}


/** \brief This function checks whether the lock is considered locked.
 *
 * This function checks whether the lock worked and is still considered
 * active, as in, it did not yet time out.
 *
 * This function does not access the network at all. It checks whether
 * the lock is still valid using the current time and the time at which
 * the LOCKED message said the lock would time out.
 *
 * If you want to know whether snaplock decided that the lock timed out
 * then you need to consider calling the lock_timedout() function instead.
 *
 * If you want to know how my time you have left on this lock, use the
 * get_timeout_date() instead and subtract time(nullptr). If positive,
 * that's the number of seconds you have left.
 *
 * \note
 * The function returns false if there is no lock connection, which
 * means that there is no a lock in effect at this time.
 *
 * \return true if the lock is still in effect.
 *
 * \sa lock_timedout()
 * \sa get_timeout_date()
 */
bool cluck::is_locked() const
{
    return f_state == state_t::CLUCK_STATE_LOCKED
        && f_lock_timeout_date > snapdev::now();
}


/** \brief Check whether the object is currently busy.
 *
 * This function checks whether the object is busy. If so, then the lock()
 * function cannot be called. It is expected that the object is and remains
 * busy between the lock() call and the receival of the UNLOCKED message.
 *
 * \note
 * This means the object cannot immediately be reused after an unlock().
 * This is because we need to get the cluck service acknowledgement
 * before we can send a new LOCK message to the cluck service from the
 * same process (TBD: I think that with are serialization we can avoid
 * the UNLOCKING state).
 *
 * \return true if the object is considered busy.
 */
bool cluck::is_busy() const
{
    return f_state != state_t::CLUCK_STATE_IDLE;
}


/** \brief Verify a messag we received.
 *
 * Many of the messages we receive have the exact same set of parameters.
 * This function makes sure that these parameters are valid.
 *
 * If the object_name or tag are not defined, or if the tag is not a match
 * then this function returns false.
 *
 * \note
 * The function expects the "object_name" and "tag" parameters to be defined.
 * These should be tested through the dispatcher with the corresponding
 * message definitions. If this function throws because of a missing
 * parameter, check those message definitions and update them accordingly.
 *
 * \exception invalid_parameter
 * If the object_name parameter is not set to this cluck f_object_name
 * value, then this exception is raised. Note that if the object_name
 * parameter or the tag parameters are not defined, the function instead
 * sends an UNKNOWN message reply and then returns false.
 *
 * \param[in] msg  The message to verify.
 *
 * \return true if the message is considered valid.
 */
bool cluck::is_cluck_msg(ed::message & msg) const
{
    // the tag must match our tag -- this allows your process to support
    // more than one lock (as long as each one has a different object name)
    //
    if(msg.get_integer_parameter(g_name_cluck_param_tag) != f_tag)
    {
        // IMPORTANT NOTE: this tag is checked in match_command_and_tag()
        //                 before our msg_...() callbacks get called so this
        //                 error should never happen
        //
        throw logic_error("tag mismatch in is_cluck_msg()."); // LCOV_EXCL_LINE
    }

    if(msg.get_parameter(g_name_cluck_param_object_name) != f_object_name)
    {
        // somehow we received a message with the wrong object name
        //
        ed::message invalid;
        invalid.user_data(msg.user_data<void>());
        invalid.reply_to(msg);
        invalid.set_command(ed::g_name_ed_cmd_invalid);
        invalid.add_parameter(ed::g_name_ed_param_command, msg.get_command());
        invalid.add_parameter(
              ed::g_name_ed_param_message
            , "the \"object_name\" parameter does not match this cluck object. Got \""
              + msg.get_parameter("object_name")
              + "\", expected \""
              + f_object_name
              + "\".");
        f_connection->send_message(invalid);
        return false;
    }

    return true;
}


/** \brief Process the timeout event.
 *
 * This function processes a timeout event. Each time we send a message
 * and require a timely reply, we start a timer to see whether it was
 * indeed timely or not. If not, then this function gets called and we
 * can proceed with canceling the request.
 *
 * This is often referenced as the local timeout.
 */
void cluck::process_timeout()
{
    set_enable(false);

    // the LOCK event and the lock duration can time out
    //
    switch(f_state)
    {
    // LCOV_EXCL_START
    case state_t::CLUCK_STATE_IDLE:
        SNAP_LOG_DEBUG
            << "process_timeout() called with state set to CLUCK_STATE_IDLE."
            << SNAP_LOG_SEND;
        snapdev::NOT_REACHED_IN_TEST();
        break;
    // LCOV_EXCL_STOP

    case state_t::CLUCK_STATE_LOCKING:
        // lock never obtained
        //
        set_reason(reason_t::CLUCK_REASON_LOCAL_TIMEOUT);
        lock_failed();
        finally();
        break;

    case state_t::CLUCK_STATE_LOCKED:
        // we are out of time, unlock now
        //
        set_reason(reason_t::CLUCK_REASON_LOCAL_TIMEOUT);
        unlock();
        break;

    case state_t::CLUCK_STATE_UNLOCKING:
        // the UNLOCK was never acknowledged
        //
        set_reason(reason_t::CLUCK_REASON_LOCAL_TIMEOUT);
        lock_failed();
        finally();
        break;

    // LCOV_EXCL_START
    case state_t::CLUCK_STATE_FAILED:
        SNAP_LOG_DEBUG
            << "process_timeout() called with state set to CLUCK_STATE_FAILED."
            << SNAP_LOG_SEND;
        snapdev::NOT_REACHED_IN_TEST();
        break;
    // LCOV_EXCL_STOP

    }
}


/** \brief This function gets called whenever the lock is in effect.
 *
 * This function is a signal telling you that the lock is in effect. You
 * are free to override it, although, in most likelihood, you'll want to
 * set a lock obtained callback instead.
 *
 * If you override the function, you are responsible to call it if you
 * want the lock obtained callbacks to be called.
 *
 * Further, the default function checks the cluck lock mode, if SIMPLE,
 * it sends the UNLOCK immediately before returning. In other words,
 * you probably will want to call this instance of the function after
 * you did the work you intended to do while the lock is active.
 *
 * \sa add_lock_obtained_callback()
 */
void cluck::lock_obtained()
{
    f_lock_obtained_callbacks.call(this);

    if(f_mode == mode_t::CLUCK_MODE_SIMPLE)
    {
        // in this case the user is done and we can just release the
        // lock automatically; otherwise the user is responsible for
        // releasing the lock once done
        //
        unlock();
    }
}


/** \brief The lock did not take or an error was reported.
 *
 * When the LOCK message does not get a reply or gets a reply implying
 * that it cannot be secured, this function gets called. You are free
 * to override the function.
 *
 * By default, this function calls all the lock failed callbacks you
 * added to this object. Make sure to call it if you use callbacks.
 *
 * \sa add_lock_failed_callback();
 */
void cluck::lock_failed()
{
    if(f_state != state_t::CLUCK_STATE_FAILED)
    {
        f_state = state_t::CLUCK_STATE_FAILED;

        // disable our timer, we don't need to time out if the lock failed
        // since we're done in this case
        //
        set_enable(false);

        f_lock_failed_callbacks.call(this);
    }
}


/** \brief The lock cycle is finally complete.
 *
 * This function is called once the lock process completes. This means
 * a LOCK was sent and we either were able to do work while the lock
 * was active or the lock failed and we received a call to the
 * lock_failed() function instead.
 *
 * By default, this function calls all the finally callbacks you
 * added to this object. Make sure to call it if you use callbacks.
 *
 * Inside the finally() function and callbacks, the state of the lock
 * is "idle". This means you can call the lock() function immediately.
 *
 * \note
 * The function makes sure to remove the dispatcher callbacks. This means
 * further events in link with this lock will be ignored. This is considered
 * normal since further events would not make sense at this point.
 */
void cluck::finally()
{
    f_state = state_t::CLUCK_STATE_IDLE;
    f_dispatcher->remove_matches(f_tag);
    f_finally_callbacks.call(this);
}


/** \brief Process the LOCKED message.
 *
 * Whenever the client sends a LOCK message to the cluckd service, it expects
 * a LOCKED reply which means the lock was obtained and is in place for this
 * process to make use of.
 *
 * The lock_obtained() function is called which in turn calls your
 * lock_obtained callbacks.
 *
 * \param[in] msg  The LOCKED message.
 */
void cluck::msg_locked(ed::message & msg)
{
    if(!is_cluck_msg(msg))
    {
        set_reason(reason_t::CLUCK_REASON_INVALID);
        lock_failed();
        finally();
        return;
    }

    f_state = state_t::CLUCK_STATE_LOCKED;
    f_lock_timeout_date = msg.get_timespec_parameter(g_name_cluck_param_timeout_date);
    f_unlocked_timeout_date = msg.get_timespec_parameter(g_name_cluck_param_unlocked_date);

    // setup our timer so it times out on that date
    //
    set_timeout_date(f_lock_timeout_date);
    set_enable(true);

    lock_obtained();
}


/** \brief Process the LOCK_FAILED message.
 *
 * This function processes the LOCK_FAILED message. This means the state
 * of this cluck object becomes FAILED which means it cannot be used to
 * obtain a lock again.
 *
 * \param[in] msg  The LOCK_FAILED message.
 */
void cluck::msg_lock_failed(ed::message & msg)
{
    if(!is_cluck_msg(msg))
    {
        set_reason(reason_t::CLUCK_REASON_INVALID);
    }
    else
    {
        std::string const error(msg.get_parameter(g_name_cluck_param_error));
        if(error == g_name_cluck_value_timedout)
        {
            set_reason(reason_t::CLUCK_REASON_REMOTE_TIMEOUT);
        }
        else
        {
            // this may be a programmer error that need fixing
            //
            SNAP_LOG_WARNING
                << "communicatord did not like our LOCK message: "
                << error
                << '.'
                << SNAP_LOG_SEND;

            set_reason(reason_t::CLUCK_REASON_INVALID);
        }
    }

    lock_failed();
    finally();
}


/** \brief Get a transmission report on errors.
 *
 * If the LOCK message cannot be sent, we receive a transmission error.
 * At the moment, there are two possible reasons:
 *
 * \li The destination is not currently available and the message was cached
 * \li The transmission failed because the requested service is not registered
 *
 * In case of a plain failure, we cancel the whole process immediately.
 *
 * In case the message was cached, we can ignored the report since the
 * message will eventually make it so it is not an immediate failure.
 * In most cases, if cached, the timeout will let us know if the message
 * does not make it.
 *
 * \note
 * At the moment we do not use the cache for any of our messages.
 *
 * \param[in] msg  The TRANSMISSION_REPORT message.
 */
void cluck::msg_transmission_report(ed::message & msg)
{
    std::string const status(msg.get_parameter(communicatord::g_name_communicatord_param_status));
    if(msg.has_parameter(communicatord::g_name_communicatord_param_command)
    && msg.get_parameter(communicatord::g_name_communicatord_param_command) == g_name_cluck_cmd_lock
    && status == communicatord::g_name_communicatord_value_failed)
    {
        SNAP_LOG_RECOVERABLE_ERROR
            << "the transmission of our \""
            << msg.get_parameter(communicatord::g_name_communicatord_param_command)
            << "\" message failed to travel to a cluckd service."
            << SNAP_LOG_SEND;

        // TODO: this message is global, so we fail all the
        //       currently valid locks in this very process
        //       (i.e. all the cluck objects have this function called
        //       on a TRANSITION_REPORT message); this is only an issue
        //       if you use threads and more than one has a lock at a
        //       time
        //
        set_reason(reason_t::CLUCK_REASON_TRANSMISSION_ERROR);
        lock_failed();
        finally();
    }
}


/** \brief Process the UNLOCK acknowledgement.
 *
 * Once the lock of a process is unlocked, the UNLOCKED message is sent to
 * that process. This function processes the acknowledgement which means
 * calling the finally() function.
 *
 * After this message was received the lock is idle again and can be
 * re-obtained by calling the lock() function.
 *
 * \param[in] msg  The message sent by the cluck service.
 */
void cluck::msg_unlocked(ed::message & msg)
{
    if(!is_cluck_msg(msg))
    {
        set_reason(reason_t::CLUCK_REASON_INVALID);
        lock_failed();
    }
    else
    {
        set_enable(false);
        if(snapdev::now() >= f_unlocked_timeout_date)
        {
            // we took too long and received the unlocked after the lock was
            // over (instead of snapdev::now() called here, we may want to
            // call it right after the callback returned)
            //
            set_reason(reason_t::CLUCK_REASON_REMOTE_TIMEOUT);
            lock_failed();
        }
        else
        {
            set_reason(reason_t::CLUCK_REASON_NONE);
        }
    }
    finally();
}



/** \brief The cluckd service sent us an UNLOCKING message.
 *
 * Whenever the cluck daemon detects that a lock is about to be unlocked,
 * it sends the UNLOCKING message to the lock holder. At that point, the
 * client is expected to stop using the lock. This function actually
 * sends the UNLOCK message to acknowledge receipt of this message.
 *
 * The client is expected to check the is_locked() function as required
 * to make sure it does not access an unlocked resource.
 *
 * \param[in] msg  The UNLOCKING message.
 */
void cluck::msg_unlocking(ed::message & msg)
{
    if(!is_cluck_msg(msg))
    {
        set_reason(reason_t::CLUCK_REASON_INVALID);
        lock_failed();
        finally();
        return;
    }

    set_reason(reason_t::CLUCK_REASON_REMOTE_TIMEOUT);
    if(snapdev::now() >= f_unlocked_timeout_date)
    {
        // it looks like we are beyond unlocking this lock
        //
        set_enable(false);
        lock_failed();
        finally();
    }
    else
    {
        unlock();
    }
}



} // namespace cluck
// vim: ts=4 sw=4 et
