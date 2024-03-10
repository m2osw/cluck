// Copyright (c) 2016-2024  Made to Order Software Corp.  All Rights Reserved
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
#include    <communicatord/names.h>


// cppthread
//
#include    <cppthread/guard.h>
#include    <cppthread/mutex.h>
#include    <cppthread/thread.h>


//// snaplogger
////
//#include    <snaplogger/message.h>
//
//
//// snapdev
////
//#include    <snapdev/not_reached.h>
//#include    <snapdev/not_used.h>
//
//
//// C++
////
//#include    <iostream>
//
//
//// C
////
//#include    <unistd.h>
//#include    <sys/syscall.h>


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
 * much more effective (if you can guarantee that all said processes will
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
 * the finally() function. By default, those functions calls your callbacks
 * which can be much more practical than deriving this class.
 *
 * Note that the cluck class is a communicator connection of type ed::timer.
 * You are responsible for adding that class to the communicator or use
 * the factory helper function.
 */

namespace cluck
{

//namespace
//{
//
//
///** \brief The default time to live of a lock.
// *
// * By default the inter-process locks are kept for only five seconds.
// * You may change the default using the
// * snaplock::initialize_lock_duration_timeout() function.
// *
// * You can specify how long a lock should be kept around by setting its
// * duration at the time you create it (see the snap_lock constructor.)
// */
//snap_lock::timeout_t g_lock_duration_timeout = snap_lock::SNAP_LOCK_DEFAULT_TIMEOUT;
//
//
///** \brief The default time to wait in order to obtain a lock.
// *
// * By default the snaplock waits five seconds to realize an
// * inter-process lock. If the locak cannot be obtained within
// * that short period of time, the locking fails.
// *
// * You may change the default using the
// * snaplock::initialize_lock_obtention_timeout() function.
// *
// * You can specify how long to wait for a lock to take by setting
// * its obtention duration at the time you create it (see the snap_lock
// * constructor.)
// *
// * \note
// * By default we use the same amount of the g_lock_duration_timeout
// * variable, which at this point is fortuitious.
// */
//snap_lock::timeout_t g_lock_obtention_timeout = snap_lock::SNAP_LOCK_DEFAULT_TIMEOUT;
//
//
///** \brief The default time to wait for a timed out lock acknowledgement.
// *
// * Whenever a lock is obtained, it can time out if the owner does not send
// * an UNLOCK message on time.
// *
// * When a lock times out, the snaplock daemon forcibly sends an UNLOCKED
// * message with an error message set to "timedout". When the service that
// * owns that lock receives that message, it is expected to acknowledge it.
// * If no acknowledgement happens before this duration elapses, then the
// * lock is released no matter what. This, indeed, means that another
// * process may obtain the same lock and access the same resources in
// * parallel...
// *
// * To acknowledge an UNLOCKED message, reply with UNLOCK. No other UNLOCKED
// * message gets sent after that.
// *
// * By default the unlock duraction is set to
// * snap_lock::SNAP_UNLOCK_MINIMUM_TIMEOUT which is one minute.
// */
//snap_lock::timeout_t g_unlock_duration_timeout = snap_lock::SNAP_UNLOCK_MINIMUM_TIMEOUT;
//
//
//#ifdef _DEBUG
///** \brief Whether the snapcommunicator parameters were initialized.
// *
// * This variable is only used in debug mode. This allows us to know whether
// * the initialize_snapcommunicator() was called before we make use of the
// * snap_lock() interface. Without that initialization, we may run in various
// * problems if the administrator changed his snapcommunicator parameters
// * such as the port to which we need to connect. This would be a bug in
// * the code.
// *
// * In release mode we ignore that flag.
// */
//bool g_snapcommunicator_initialized = false;
//#endif
//
//
///** \brief The default snapcommunicator address.
// *
// * This variable holds the snapcommunicator IP address used to create
// * locks by connecting (Sending messages) to snaplock processes.
// *
// * It can be changed using the snaplock::initialize_snapcommunicator()
// * function.
// */
//std::string g_snapcommunicator_address = "127.0.0.1";
//
//
///** \brief The default snapcommunicator port.
// *
// * This variable holds the snapcommunicator port used to create
// * locks by connecting (Sending messages) to snaplock processes.
// *
// * It can be changed using the snaplock::initialize_snapcommunicator()
// * function.
// */
//int g_snapcommunicator_port = 4040;
//
//
///** \brief The default snapcommunicator mode.
// *
// * This variable holds the snapcommunicator mode used to create
// * locks by connecting (Sending messages) to snaplock processes.
// *
// * It can be changed using the snaplock::initialize_snapcommunicator()
// * function.
// */
//tcp_client_server::bio_client::mode_t g_snapcommunicator_mode = tcp_client_server::bio_client::mode_t::MODE_PLAIN;
//
//
///** \brief A unique number used to name the lock service.
// *
// * Each time we create a new lock service we need to have a new unique name
// * because otherwise we could receive replies from a previous lock and
// * there is no other way for us to distinguish them. This is important
// * only if the user is trying to lock the same object several times in
// * a row.
// */
//int g_unique_number = 0;
//
//
//}
//// no name namespace
//
//
//
//
//namespace detail
//{
//
///** \brief The actual implementation of snap_lock.
// *
// * This class is the actual implementation of snap_lock which is completely
// * hidden from the users of snap_lock. (i.e. implementation details.)
// *
// * The class connects to the snapcommunicator daemon, sends a LOCK event
// * and waits for the LOCKED message or a failure at which point the
// * daemon returns.
// *
// * The connection remains _awake_ even once the lock was obtained in
// * case the snaplock daemon wants to send us another message and so
// * we can send it an UNLOCK message. It is also important to check
// * whether the lock timed out after a while. The snaplock daemon
// * sends us an UNLOCKED message which we need to acknowledge with
// * an UNLOCK. This is important to allow the next user in line to
// * quickly obtain his own lock.
// */
//class messenger
//    : public communicatord::communicator
//{
//public:
//    typedef std::shared_ptr<messenger>  pointer_t;
//
//                            messenger(
//                                  std::string const & object_name
//                                , snap_lock::timeout_t lock_duration
//                                , snap_lock::timeout_t lock_obtention_timeout
//                                , snap_lock::timeout_t unlock_duration);
//    virtual                 ~messenger() override;
//
//    void                    run();
//    void                    unlock();
//
//    bool                    is_locked() const;
//    bool                    lock_timedout() const;
//    time_t                  get_lock_timeout_date() const;
//
//    // implementation of snap_communicator::snap_tcp_blocking_client_message_connection
//    virtual void            process_timeout() override;
//    //virtual void            process_message(snap_communicator_message const & message) override;
//
//    // implementation of snap_communicator::connection_with_send_message
//    virtual void            ready(snap_communicator_message & message) override;
//    virtual void            stop(bool quitting) override;
//
//private:
//    void                    msg_locked(snap::snap_communicator_message & message);
//    void                    msg_lockfailed(snap::snap_communicator_message & message);
//    void                    msg_unlocked(snap::snap_communicator_message & message);
//
//    static snap::dispatcher<lock_connection>::dispatcher_match::vector_t const    g_lock_connection_messages;
//
//    pid_t const                 f_owner;
//    std::string const           f_service_name;
//    std::string const           f_object_name;
//    snap_lock::timeout_t const  f_lock_duration;
//    time_t                      f_lock_timeout_date = 0;
//    time_t const                f_obtention_timeout_date;
//    snap_lock::timeout_t const  f_unlock_duration = snap_lock::SNAP_UNLOCK_USES_LOCK_TIMEOUT;
//    bool                        f_lock_timedout = false;
//};
//
//
//
//snap::dispatcher<connection>::dispatcher_match::vector_t const connection::g_connection_messages =
//{
//    {
//        g_name_cluck_cmd_locked
//      , &lock_connection::msg_locked
//    },
//    {
//        g_name_cluck_cmd_lockfailed
//      , &lock_connection::msg_lockfailed
//    },
//    {
//        g_name_cluck_cmd_unlocked
//      , &lock_connection::msg_unlocked
//    }
//};
//
//
//
///** \brief Initiate an inter-process lock.
// *
// * This constructor is used to obtain an inter-process lock.
// *
// * The lock will be effective on all the computers that have access to
// * the running snaplock instances you can connect to via snapcommunicator.
// *
// * The LOCK and other messages are sent to the snaplock daemon using
// * snapcommunicator.
// *
// * Note that the constructor creates a "lock service" which is given a
// * name which is "lock" and the current thread identifier and a unique
// * number. We use an additional unique number to make sure messages
// * sent to our old instance(s) do not make it to a newer instance, which
// * could be confusing and break the lock mechanism in case the user
// * was trying to get a lock multiple times on the same object.
// *
// * In a perfect world, the following shows what happens, message wise,
// * as far as the client is concerned. The snaplock sends many more
// * messages in order to obtain the lock (See src/snaplock/snaplock_ticket.cpp
// * for details about those events.)
// *
// * \note
// * The REGISTER message is sent from the constructor to initiate the
// * process. This function starts by receiving the READY message.
// *
// * \msc
// *    client,snapcommunicator,snaplock;
// *
// *    client->snapcommunicator [label="REGISTER"];
// *    snapcommunicator->client [label="READY"];
// *    snapcommunicator->client [label="HELP"];
// *    client->snapcommunicator [label="COMMANDS"];
// *    client->snapcommunicator [label="LOCK"];
// *    snapcommunicator->snaplock [label="LOCK"];
// *    snaplock->snapcommunicator [label="LOCKED"];
// *    snapcommunicator->client [label="LOCKED"];
// *    ...;
// *    client->snapcommunicator [label="UNLOCK"];
// *    snapcommunicator->snaplock [label="UNLOCK"];
// *    client->snapcommunicator [label="UNREGISTER"];
// * \endmsc
// *
// * If somehow the lock fails, we may receive LOCKFAILED or UNLOCKED instead
// * of the LOCK message.
// *
// * \warning
// * The g_unique_number and the other global variables are not handle
// * safely if you attempt locks in a multithread application. The
// * unicity of the lock name will still be correct because the name
// * includes the thread identifier (see gettid() function.) In a
// * multithread application, the initialize_...() function should be
// * called once by the main thread before the other threads get created.
// *
// * \param[in] object_name  The name of the object being locked.
// * \param[in] lock_duration  The amount of time the lock will last if obtained.
// * \param[in] lock_obtention_timeout  The amount of time to wait for the lock.
// * \param[in] unlock_duration  The amount of time we have to acknowledge a
// *                             timed out lock.
// */
//messenger::messenger(
//          std::string const & object_name
//        , snap_lock::timeout_t lock_duration
//        , snap_lock::timeout_t lock_obtention_timeout
//        , snap_lock::timeout_t unlock_duration)
//    : snap_tcp_blocking_client_message_connection(g_snapcommunicator_address, g_snapcommunicator_port, g_snapcommunicator_mode)
//    , dispatcher(this, g_lock_connection_messages)
//    , f_owner(cppthread::gettid())
//    , f_service_name(std::string("lock_%1_%2").arg(cppthread::gettid()).arg(++g_unique_number))
//    , f_object_name(object_name)
//    , f_lock_duration(lock_duration == -1 ? g_lock_duration_timeout : lock_duration)
//    //, f_lock_timeout_date(0) -- only determined if we obtain the lock
//    , f_obtention_timeout_date((lock_obtention_timeout == -1 ? g_lock_obtention_timeout : lock_obtention_timeout) + time(nullptr))
//    , f_unlock_duration(unlock_duration)
//{
//#ifdef _DEBUG
//    if(!g_snapcommunicator_initialized)
//    {
//        throw snap_lock_not_initialized("your process must call snap::snap_lock::initialize_snapcommunicator() at least once before you can create locks.");
//    }
//#endif
//    add_snap_communicator_commands();
//
//    // tell the lower level when the lock obtention times out;
//    // that one is in microseconds; if we do obtain the lock,
//    // the timeout is then changed to the lock duration
//    // (which is computed from the time at which we get the lock)
//    //
//    set_timeout_date(f_obtention_timeout_date * 1000000L);
//}
//
//
///** \brief Send the UNLOCK message to snaplock to terminate the lock.
// *
// * The destructor makes sure that the lock is released.
// */
//connection::~connection()
//{
//    try
//    {
//        unlock();
//    }
//    catch(snap_communicator_invalid_message const &)
//    {
//    }
//}
//
//
///** \brief Listen for messages.
// *
// * This function overloads the blocking connection run() function so we
// * can properly initialize the lock_connection object (some things just
// * cannot be done in the construtor.)
// *
// * The function makes sure we have the dispatcher setup and sends the
// * REGISTER message so we get registered with snapcommunicator.
// */
//void connection::run()
//{
//    set_dispatcher(std::dynamic_pointer_cast<lock_connection>(shared_from_this()));
//
//    // need to register with snap communicator
//    //
//    snap::snap_communicator_message register_message;
//    register_message.set_command(communicatord::g_name_communicatord_cmd_register);
//    register_message.add_parameter(communicatord::g_name_communicatord_param_service, f_service_name);
//    register_message.add_parameter(communicatord::g_name_communicatord_param_version, communicatord::VERSION);
//    send_message(register_message);
//
//    // now wait for the READY and HELP replies, send LOCK, and
//    // either timeout or get the LOCKED message
//    //
//    snap_tcp_blocking_client_message_connection::run();
//}
//
//
///** \brief Send the UNLOCK early (before the destructor is called).
// *
// * This function releases the lock obtained by the constructor.
// *
// * It is safe to call the function multiple times. It will send the
// * UNLOCK only the first time.
// *
// * Note that it is not possible to re-obtain the lock once unlocked.
// * You will have to create a new lock_conenction object to do so.
// *
// * \note
// * If you fork or attempt to unlock from another thread, the unlock()
// * function will do nothing. Only the exact thread that created the
// * lock can actually unlock. This does happen in snap_backend children
// * which attempt to remove the lock their parent setup because the
// * f_lock variable is part of the connection which is defined in a
// * global variable.
// */
//void lock_connection::unlock()
//{
//    if(f_lock_timeout_date != 0
//    && f_owner == gettid())
//    {
//        f_lock_timeout_date = 0;
//
//        // done
//        //
//        // explicitly send the UNLOCK message and then make sure to unregister
//        // from snapcommunicator; note that we do not wait for a reply to the
//        // UNLOCK message, since to us it does not matter much as long as the
//        // message was sent...
//        //
//        snap_communicator_message unlock_message;
//        unlock_message.set_command("UNLOCK");
//        unlock_message.set_service("snaplock");
//        unlock_message.add_parameter("object_name", f_object_name);
//        unlock_message.add_parameter("pid", gettid());
//        send_message(unlock_message);
//
//        snap_communicator_message unregister_message;
//        unregister_message.set_command("UNREGISTER");
//        unregister_message.add_parameter("service", f_service_name);
//        send_message(unregister_message);
//    }
//}
//
//
///** \brief Check whether the lock worked (true) or not (false).
// *
// * This function returns true if the lock succeeded and is still
// * active. This function detects whether the lock timed out and
// * returns false if so.
// *
// * The following is a simple usage example. Note that the UNLOCK
// * message will be sent to the snaplock daemon whenever the
// * snap_lock get destroyed and a lock was obtained. There is
// * no need for you to call the unlock() function. However, it can
// * be useful to perform the very important tasks on the resource
// * being locked first and if it times out, forfeit further less
// * important work.
// *
// * \code
// *      {
// *          snap_lock l(...);
// *
// *          ...do work with resource...
// *          if(l.is_locked())
// *          {
// *              ...do more work with resource...
// *          }
// *          // l.unlock() is implicit
// *      }
// * \endcode
// *
// * You may check how long the lock has left using the
// * get_lock_timeout_date() which is the date when the lock
// * times out.
// *
// * Note that the get_lock_timeout_date() function returns zero
// * if the lock was not obtained and a threshold date in case the
// * lock was obtained, then zero again after a call to the unlock()
// * function or when the UNLOCKED message was received and processed
// * (i.e. to get the UNLOCKED message processed you need to call the
// * lock_timedout() function).
// *
// * \return true if the lock is currently active.
// *
// * \sa get_lock_timeout_date()
// * \sa unlock()
// * \sa lock_timedout()
// */
//bool lock_connection::is_locked() const
//{
//    // if the lock timeout date was not yet defined, it is not yet
//    // locked (we may still be trying to obtain the lock, though);
//    // one not zero, the lock is valid as long as that date is
//    // after 'now'
//    //
//    return f_lock_timeout_date != 0 && f_lock_timeout_date > time(nullptr);
//}
//
//
///** \brief Check whether the lock timed out.
// *
// * This function checks whether an UNLOCKED was received while you hold
// * the lock. You cannot call the function if you did not yet obtain the
// * lock.
// *
// * If you are going to destroy the lock right away after determining that
// * it timed out, call the is_locked() function instead. The lock object
// * will automatically send an UNLOCK message when it gets destroyed so
// * just that is enough.
// *
// * Now, you want to use this lock_timedout() function in case you want
// * to test whether a lock is about to be released by the snaplock daemon
// * which took ownship of the lock. It will not send the UNLOCK event
// * back (acknowlegement). You are responsible to call the unlock()
// * function once this function returns true.
// *
// * This function is used on a snap_lock object that is kept around
// * for a while. If you are going to destroy the lock anyway, you
// * can just do that as soon as possible and you will be just fine.
// *
// * The following more or less shows a case where the lock_timedout()
// * and unlock() can be used. The lock_timedout() function can be
// * called any number of time, so if you do work in a loop, you can
// * safely call it once or twice per iteration:
// *
// * \code
// *  snap_lock l;
// *
// *  for(;;)
// *  {
// *      l.lock(...);
// *
// *      do
// *      {
// *          ...start synchronized work here...
// *          if(l.lock_timedout())
// *          {
// *              // make sure we unlock
// *              l.unlock();
// *              break;
// *          }
// *          ...more synchronized work
// *      }
// *      while(false);
// *  }
// * \endcode
// *
// * This example shows an explicit unlock() call. If you immediately
// * try a new lock() call, then the unlock() is call implicitely.
// *
// * \note
// * As mentioned above, you still got a little bit of time after this
// * function returns true. However, the sooner you call the unlock()
// * function after this function returns true, the safer.
// *
// * \return true if the lock timed out, false otherwise.
// *
// * \sa is_locked()
// * \sa unlock()
// */
//bool lock_connection::lock_timedout() const
//{
//    if(f_lock_timeout_date != 0)
//    {
//        // if the timeout date has not yet elapsed then the lock cannot
//        // have dropped yet (unless a big problem happened and checking
//        // with snaplock would probably fail anyway.)
//        //
//        if(f_lock_timeout_date > time(nullptr))
//        {
//            return false;
//        }
//
//        // it looks like we timed out, check for an UNLOCKED event
//        //
//        // just do a peek(), that is enough since the msg_unlocked()
//        // will set the f_lock_timedout flag as required; if we did
//        // not yet receive a full event, we return false (i.e. not
//        // yet timed out); it will also set f_lock_timeout_date back
//        // to zero
//        //
//        const_cast<lock_connection *>(this)->peek();
//    }
//
//    return f_lock_timedout;
//}
//
//
///** \brief Retrieve the cutoff time for this lock.
// *
// * This lock will time out when this date is reached.
// *
// * If the value is zero, then the lock was not yet obtained.
// *
// * \return The lock timeout date
// */
//time_t lock_connection::get_lock_timeout_date() const
//{
//    return f_lock_timeout_date;
//}
//
//
///** \brief The lock was not obtained in time.
// *
// * This function gets called whenever the lock does not take with
// * the 'obtention_timeout' amount.
// *
// * Here we tell the system we are done with the lock so that way the
// * run() function returns silently (instead of throwing an error.)
// *
// * The lock will not be active so a call to is_locked() will say
// * so (i.e. return false.)
// */
//void lock_connection::process_timeout()
//{
//    mark_done();
//}
//
//
///** \brief The snapcommunicator is ready to talk to us.
// *
// * This function gets called once the connection between the snapcommunicator
// * and us is up and running.
// *
// * We immediately send the LOCK message to the snaplock running on this
// * system.
// */
//void lock_connection::ready(snap_communicator_message & message)
//{
//    snapdev::NOT_USED(message);
//
//    // no reply expected from the COMMANDS message,
//    // so send the LOCK now which initiates the locking
//    //
//    snap_communicator_message lock_message;
//    lock_message.set_command("LOCK");
//    lock_message.set_service("snaplock");
//    lock_message.add_parameter("object_name", f_object_name);
//    lock_message.add_parameter("pid", gettid());
//    lock_message.add_parameter("timeout", f_obtention_timeout_date);
//    lock_message.add_parameter("duration", f_lock_duration);
//    if(f_unlock_duration != snap_lock::SNAP_UNLOCK_USES_LOCK_TIMEOUT)
//    {
//        lock_message.add_parameter("unlock_duration", f_unlock_duration);
//    }
//    send_message(lock_message);
//}
//
//
///** \brief Stop the lock connections.
// *
// * Whenever a STOP or QUITTING is received this function gets called.
// *
// * We should never receive these messages in a lock_connection, although
// * it is definitely possible and part of the protocol. What is more likely
// * to happen is that the function
// *
// * \param[in] quitting  Whether the sending is telling us that we are quitting
// *            (i.e. rebooting) or just stopping.
// */
//void lock_connection::stop(bool quitting)
//{
//    SNAP_LOG_WARNING
//        << "we received the "
//        << (quitting ? "QUITTING" : "STOP")
//        << " command while waiting for a lock."
//        << SNAP_LOG_SEND;
//
//    mark_done();
//}
//
//
///** \brief Process the LOCKED message.
// *
// * This function processes the LOCKED message meaning that it saves the
// * timeout_date as determined by the snaplock daemon and mark the message
// * loop as done which means it will return to the caller.
// *
// * Note however that the TCP/IP connection to snapcommunicator is kept
// * alive since we want to keep the lock until we are done with it.
// *
// * \param[in] message  The message we just received.
// */
//void lock_connection::msg_locked(snap::snap_communicator_message & message)
//{
//    // the lock worked?
//    //
//    if(message.get_parameter("object_name") != f_object_name)
//    {
//        // somehow we received the LOCKED message with the wrong object name
//        //
//        throw snap_lock_failed_exception(
//                  "received lock confirmation for object \""
//                + message.get_parameter("object_name")
//                + "\" instead of \""
//                + f_object_name
//                + "\" (LOCKED).");
//    }
//    f_lock_timeout_date = message.get_integer_parameter("timeout_date");
//
//    // change the timeout to the new date, since we are about to
//    // quit the run() loop anyway, it should not be necessary
//    //
//    set_timeout_date(f_lock_timeout_date * 1000000L);
//
//    // release hand back to the user while lock is still active
//    //
//    mark_done();
//}
//
//
///** \brief Process the LOCKFAILED message.
// *
// * This function processes the LOCKFAILED message meaning that it saves
// * the lock was never obtained. In most cases this is due to a timeout.
// * The timeout can be due to the fact that the snaplock too is not yet
// * handling locks (LOCKREADY was not yet sent.)
// *
// * Since the lock failed, you must not make use of the resource(s) you
// * were trying to lock.
// *
// * \param[in] message  The message we just received.
// */
//void lock_connection::msg_lockfailed(snap::snap_communicator_message & message)
//{
//    if(message.get_parameter("object_name") == f_object_name)
//    {
//        SNAP_LOG_WARNING
//            << "lock for object \""
//            << f_object_name
//            << "\" failed (LOCKEDFAILED) with reason: "
//            << message.get_parameter("error")
//            << "."
//            << SNAP_LOG_SEND;
//    }
//    else
//    {
//        // this should not happen
//        //
//        SNAP_LOG_ERROR
//            << "object \""
//            << message.get_parameter("object_name")
//            << "\" just reported a lock failure and we received its message while trying to lock \""
//            << f_object_name
//            << "\" (LOCKEDFAILED) with reason: "
//            << message.get_parameter("error")
//            << "."
//            << SNAP_LOG_SEND;
//    }
//
//    mark_done();
//}
//
//
///** \brief Process the UNLOCKLED message.
// *
// * This function processes the UNLOCKED message.
// *
// * There are three potential cases when we can receive an UNLOCKED:
// *
// * \li a spurious message -- i.e. the message was not meant for this lock;
// *     this is not ever expected, we throw when this happens
// * \li lock timed out -- if your process takes too long before releasing
// *     the lock, you get this type of UNLOCKED with an error message
// *     saying "timedout"
// * \li acknowledgement -- when we send the UNLOCK message to a snaplock, it
// *     acknowledges with UNLOCKED (unless we already received the UNLOCKED
// *     because of a timeout, then it does not get sent twice)
// *
// * The first case (object name mismatched) is just not likely to ever
// * occur. It is more of a sanity test.
// *
// * The second case (lock timed out) can happen if your task takes longer
// * then expected. In that case, you should end your task, at least the
// * part that required the locked resources. You task must then acknowledge
// * that it is done by sending an UNLOCK message to snaplock.
// *
// * The third case (acknowledgement) is sent when you initiate the
// * release of your lock by sending the UNLOCK message (see unlock()
// * for more details.)
// *
// * \attention
// * In the second case (lock timed out) the lock will still be in place
// * until your send the UNLOCK message or the amount of time specified
// * in the unlock duration parameter. By default (which is also the minimum,)
// * this is 60 seconds. You can still be accessing your resources, but should
// * consider stopping as soon as possible and re-obtain a new lock to
// * continue your work.
// *
// * \exception snap_lock_failed_exception
// * This exception is raised when the UNLOCKED is received unexpectendly
// * (i.e. the object name does not match or the UNLOCKED is received before
// * the LOCKED was ever received.)
// *
// * \param[in] message  The message we just received.
// */
//void lock_connection::msg_unlocked(snap::snap_communicator_message & message)
//{
//    // we should never receive an UNLOCKED from the wrong object
//    // because each lock attempt is given a new name
//    //
//    if(message.get_parameter("object_name") != f_object_name)
//    {
//        f_lock_timeout_date = 0;
//        SNAP_LOG_FATAL
//            << "object \""
//            << message.get_parameter("object_name")
//            << "\" just got unlocked and we received its message while trying to lock \""
//            << f_object_name
//            << "\" (UNLOCKED)."
//            << SNAP_LOG_SEND;
//        throw snap_lock_failed_exception(
//                      "object \""
//                    + message.get_parameter("object_name")
//                    + "\" just got unlocked and we received its message while trying to lock \""
//                    + f_object_name
//                    + "\" (UNLOCKED).");
//    }
//
//    // we should not receive the UNLOCKED before the LOCKED
//    // also snaplock prevents the sending of more than one
//    // UNLOCKED event so the following should never be true
//    //
//    if(f_lock_timeout_date == 0)
//    {
//        SNAP_LOG_FATAL
//            << "lock for object \""
//            << f_object_name
//            << "\" failed (UNLOCKED received before LOCKED)."
//            << SNAP_LOG_SEND;
//        throw snap_lock_failed_exception(
//                      "lock for object \""
//                    + f_object_name
//                    + "\" failed (UNLOCKED received before LOCKED).");
//    }
//
//    f_lock_timeout_date = 0;
//
//    if(message.has_parameter("error"))
//    {
//        std::string const error(message.get_parameter("error"));
//        if(error == "timedout")
//        {
//            SNAP_LOG_INFO
//                << "lock for object \""
//                << f_object_name
//                << "\" timed out."
//                << SNAP_LOG_SEND;
//
//            // we are expected to send an acknowledgement in this case
//            // but we do not do so here, the caller is expected to take
//            // care of that by calling the unlock() function explicitly
//            //
//            f_lock_timedout = true;
//        }
//        else
//        {
//            SNAP_LOG_WARNING
//                << "lock for object \""
//                << f_object_name
//                << "\" failed with error: "
//                << error
//                << "."
//                << SNAP_LOG_SEND;
//        }
//    }
//}
//
//
//
//
//
//
//
//}
//// details namespace





namespace
{



//static constexpr timeout_t  CLUCK_DEFAULT_TIMEOUT = -1;
//static constexpr timeout_t  CLUCK_MAXIMUM_TIMEOUT = 7 * 24 * 60 * 60 * 1'000'000;     // no matter what limit all timeouts to this value (7 days)
//static constexpr timeout_t  CLUCK_LOCK_DEFAULT_TIMEOUT = 5 * 1'000'000;
//static constexpr timeout_t  CLUCK_UNLOCK_DEFAULT_TIMEOUT = 5 * 1'000'000;
//static constexpr timeout_t  CLUCK_LOCK_MINIMUM_TIMEOUT = 3 * 1'000'000;
//static constexpr timeout_t  CLUCK_LOCK_OBTENTION_MAXIMUM_TIMEOUT = 60 * 60 * 1'000'000;    // by default limit obtention timeout to this value
//static constexpr timeout_t  CLUCK_UNLOCK_MINIMUM_TIMEOUT = 60 * 1'000'000;


cppthread::mutex            g_mutex = cppthread::mutex();
ed::dispatcher_match::tag_t g_tag = 0;
cluck::serial_t             g_serial = cluck::serial_t();
timeout_t                   g_lock_obtention_timeout = CLUCK_LOCK_OBTENTION_DEFAULT_TIMEOUT;
timeout_t                   g_lock_duration_timeout = CLUCK_LOCK_DURATION_DEFAULT_TIMEOUT;
timeout_t                   g_unlock_timeout = CLUCK_UNLOCK_DEFAULT_TIMEOUT;


ed::dispatcher_match::tag_t get_next_tag()
{
    cppthread::guard lock(g_mutex);
    ++g_tag;
    if(g_tag == ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG)
    {
        g_tag = 1;
    }
    return g_tag;
}


cluck::serial_t get_next_serial()
{
    cppthread::guard lock(g_mutex);
    ++g_serial;
    if(g_serial == 0)
    {
        g_serial = 1;
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
    cppthread::guard lock(g_mutex);
    if(timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        g_lock_obtention_timeout = CLUCK_LOCK_OBTENTION_DEFAULT_TIMEOUT;
    }
    else
    {
        g_lock_obtention_timeout = std::clamp(timeout, CLUCK_MINIMUM_TIMEOUT, CLUCK_LOCK_OBTENTION_MAXIMUM_TIMEOUT);
    }
}


timeout_t get_lock_duration_timeout()
{
    cppthread::guard lock(g_mutex);
    return g_lock_duration_timeout;
}


void set_lock_duration_timeout(timeout_t timeout)
{
    cppthread::guard lock(g_mutex);
    if(timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        g_lock_duration_timeout = CLUCK_LOCK_DURATION_DEFAULT_TIMEOUT;
    }
    else
    {
        g_lock_duration_timeout = std::clamp(timeout, CLUCK_MINIMUM_TIMEOUT, CLUCK_MAXIMUM_TIMEOUT);
    }
}


timeout_t get_unlock_timeout()
{
    cppthread::guard lock(g_mutex);
    return g_unlock_timeout;
}


void set_unlock_timeout(timeout_t timeout)
{
    cppthread::guard lock(g_mutex);
    if(timeout == CLUCK_DEFAULT_TIMEOUT)
    {
        g_unlock_timeout = CLUCK_UNLOCK_DEFAULT_TIMEOUT;
    }
    else
    {
        g_unlock_timeout = std::clamp(timeout, CLUCK_UNLOCK_MINIMUM_TIMEOUT, CLUCK_MAXIMUM_TIMEOUT);
    }
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
    , f_lock_obtention_timeout(get_lock_obtention_timeout())
    , f_lock_duration_timeout(get_lock_duration_timeout())
    , f_unlock_timeout(get_unlock_timeout())
{
    set_enable(false);
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


/** \brief Retrieve the lock type.
 *
 * The lock can be given a type changing the behavior of the locking
 * mechanism. Please see the set_type() function for additional details.
 *
 * \return The type of lock this cluck object uses at the moment.
 */
type_t cluck::get_type() const
{
    return f_type;
}


/** \brief Set the lock type.
 *
 * This function changes the type of lock the lock() function uses. It can
 * only be claled if the cluck object is not currently busy (i.e. before
 * the lock() function is called).
 *
 * The supported type of licks are:
 *
 * \li Exclusive Lock (type_t::CLUCK_TYPE_READ_WRITE) -- this type of lock
 * can only be obtained by one single process. This is the default. 
 * \li Shared Lock (type_t::CLUCK_TYPE_READ_ONLY) -- this type of lock can
 * be obtained by any number of processes. It, however, prevents exclusive
 * locks to take.
 * \li Exclusive Lock with Priority (type_t::CLUCK_TYPE_READ_WRITE_PRIORITY) --
 * this is similar to the basic Exclusive Lock, except that it prevents further
 * Shared Locks from happening, making sure that the exclusive lock happens
 * as soon as possible.
 *
 * \exception 
 *
 * \param[in] type  One of the type_t enumerations.
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


/** \brief Attempt a lock.
 *
 * This function attempts a lock. If a lock was already initiated or is in
 * place, the function fails.
 *
 * On a time scale, the lock obtention and lock duration parameters are used
 * as follow:
 *
 * \code
 *      lock obtention     lock duration                     unlock timeout
 *   |<---------------->|<------------------------------->|<------------>|
 *   |                  |                                 |              |
 * --+------------------+---------------------------------+--------------+
 *   ^        ^         ^      ^                          ^              ^
 *   |        |         |      |                          |              |
 *   |        |         |      |   safe work ends here ---+              |
 *   |        |         |      +--- do your work safely                  |
 *   |        |         +--- if lock not obtained, lock_failed() called  |
 *   |        +--- LOCKED received, lock_obtained() called               |
 *   +--- now                          too late to do additional work ---+
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
 * \note
 * This function does not trigger any calls to your callbacks. These only
 * happen if this function returns true and once we receive the replies.
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
    lock_message.set_service("cluck");
    lock_message.add_parameter("object_name", f_object_name);
    lock_message.add_parameter("pid", cppthread::gettid());
    lock_message.add_parameter("serial", f_serial);
    lock_message.add_parameter("timeout", obtention_timeout_date);
    if(f_lock_duration_timeout != CLUCK_DEFAULT_TIMEOUT)
    {
        lock_message.add_parameter("duration", f_lock_duration_timeout);
    }
    if(f_unlock_timeout != CLUCK_DEFAULT_TIMEOUT)
    {
        lock_message.add_parameter("unlock_duration", f_unlock_timeout);
    }
    if(f_type != type_t::CLUCK_TYPE_READ_WRITE)
    {
        lock_message.add_parameter("type", static_cast<int>(f_type));
    }
    if(!f_connection->send_message(lock_message))
    {
        return false;
    }

    set_timeout_date(obtention_timeout_date);
    set_enable(true);

    f_state = state_t::CLUCK_STATE_LOCKING;

    // start listening to our messages
    //
    ed::dispatcher_match locked(ed::define_match(
              ed::Expression(g_name_cluck_cmd_locked)
            , ed::Callback(std::bind(&cluck::msg_locked, this, std::placeholders::_1))
            , ed::Tag(f_tag)));
    f_dispatcher->add_match(locked);

    ed::dispatcher_match lock_failed(ed::define_match(
              ed::Expression(g_name_cluck_cmd_lock_failed)
            , ed::Callback(std::bind(&cluck::msg_lock_failed, this, std::placeholders::_1))
            , ed::Tag(f_tag)));
    f_dispatcher->add_match(lock_failed);

    ed::dispatcher_match unlocked(ed::define_match(
              ed::Expression(g_name_cluck_cmd_unlocked)
            , ed::Callback(std::bind(&cluck::msg_unlocked, this, std::placeholders::_1))
            , ed::Tag(f_tag)));
    f_dispatcher->add_match(unlocked);

    ed::dispatcher_match unlocking(ed::define_match(
              ed::Expression(g_name_cluck_cmd_unlocking)
            , ed::Callback(std::bind(&cluck::msg_unlocking, this, std::placeholders::_1))
            , ed::Tag(f_tag)));
    f_dispatcher->add_match(unlocking);

    ed::dispatcher_match transmission_report(ed::define_match(
              ed::Expression(communicatord::g_name_communicatord_cmd_transmission_report)
            , ed::Callback(std::bind(&cluck::msg_transmission_report, this, std::placeholders::_1))
            , ed::MatchFunc(&ed::one_to_one_callback_match)
            , ed::Tag(f_tag)
            , ed::Priority(ed::dispatcher_match::DISPATCHER_MATCH_CALLBACK_PRIORITY)));
    f_dispatcher->add_match(transmission_report);

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
    //if(f_lock_timeout_date != 0
    //&& f_owner == gettid())
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
    unlock_message.set_service("cluck");
    unlock_message.add_parameter("object_name", f_object_name);
    unlock_message.add_parameter("pid", gettid());
    unlock_message.add_parameter("serial", f_serial);
    if(!f_connection->send_message(unlock_message))
    {
        f_state = state_t::CLUCK_STATE_FAILED;
        return;
    }

    // give the UNLOCK 5 seconds to happen, if it does not happen, we'll
    // set the state to "failed" and still call the finally() callbacks
    //
    timeout_t unlock_timeout_date(snapdev::now());
    unlock_timeout_date += timeout_t(5, 0);
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
        && f_lock_timeout_date < snapdev::now();
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
    // this is one of our messages, all of them should have the "tag"
    // and "object_name" parameters, if not present, we've got a problem
    //
    if(!msg.has_parameter("object_name")
    || !msg.has_parameter("tag"))
    {
        ed::message unknown;
        unknown.user_data(msg.user_data<void>());
        unknown.reply_to(msg);
        unknown.set_command(ed::g_name_ed_cmd_unknown);
        unknown.add_parameter("command", msg.get_command());
        unknown.add_parameter("message", "the \"object_name\" and \"tag\" parameters are mandatory.");
        f_connection->send_message(unknown);
        return false;
    }

    // the tag must match our tag -- this allows your process to support
    // more than one lock (as long as each one has a different object name)
    //
    if(msg.get_integer_parameter("tag") != f_tag)
    {
        return false;
    }

    if(msg.get_parameter("object_name") != f_object_name)
    {
        // somehow we received a message with the wrong object name
        //
        throw invalid_parameter(
                  "received message \""
                + msg.get_command()
                + "\" for lock \""
                + msg.get_parameter("object_name")
                + "\" instead of \""
                + f_object_name
                + "\".");
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
    case state_t::CLUCK_STATE_IDLE:
        SNAP_LOG_DEBUG
            << "process_timeout() called with state set to CLUCK_STATE_IDLE."
            << SNAP_LOG_SEND;
        break;

    case state_t::CLUCK_STATE_LOCKING:
        // lock never obtained
        //
        set_reason(reason_t::CLUCK_REASON_LOCAL_TIMEOUT);
        lock_failed();
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
        f_state = state_t::CLUCK_STATE_FAILED;
        finally();
        break;

    case state_t::CLUCK_STATE_FAILED:
        SNAP_LOG_DEBUG
            << "process_timeout() called with state set to CLUCK_STATE_FAILED."
            << SNAP_LOG_SEND;
        break;

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
    f_state = state_t::CLUCK_STATE_FAILED;

    // disable our timer, we don't need to time out if the lock failed
    // since we're done in this case
    //
    set_enable(false);

    f_lock_failed_callbacks.call(this);
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
 */
void cluck::finally()
{
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
        return;
    }

    f_lock_timeout_date = msg.get_timespec_parameter("timeout_date");

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
        return;
    }

    if(msg.has_parameter(g_name_cluck_param_error))
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
}


/** \brief Get a transmission report on errors.
 *
 * If the LOCK message cannot be sent, we receive a transmission error.
 * At the moment, there are two possible reasons:
 *
 * \li The destination is not currently available and the message was cached
 * \li The transmission failed because the requested service is not registered
 *
 * In case of a plain failure, we cancel the whole process.
 *
 * In case the message was cached, we can ignored the report since the
 * message will eventually make it so it is not an immediate failure.
 * In most cases, if cached, the timeout will let us know if the message
 * does not make it.
 *
 * \param[in] msg  The TRANSMISSION_REPORT message.
 */
void cluck::msg_transmission_report(ed::message & msg)
{
    std::string const status(msg.get_parameter(communicatord::g_name_communicatord_param_status));
    if(msg.get_command() == g_name_cluck_cmd_lock
    && status == communicatord::g_name_communicatord_value_failed)
    {
        SNAP_LOG_RECOVERABLE_ERROR
            << "the transmission of our \""
            << msg.get_command()
            << "\" message failed to travel to a cluckd service."
            << SNAP_LOG_SEND;

        set_reason(reason_t::CLUCK_REASON_TRANSMISSION_ERROR);
        lock_failed();
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
        return;
    }

    f_state = state_t::CLUCK_STATE_IDLE;
    set_enable(false);
    set_reason(reason_t::CLUCK_REASON_NONE);
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
        return;
    }

    set_enable(false);
    set_reason(reason_t::CLUCK_REASON_REMOTE_TIMEOUT);
    unlock();
}



} // namespace cluck
// vim: ts=4 sw=4 et
