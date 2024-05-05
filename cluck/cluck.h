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
#pragma once

// eventdispatcher
//
#include    <eventdispatcher/connection_with_send_message.h>
#include    <eventdispatcher/dispatcher.h>
#include    <eventdispatcher/timer.h>


// snapdev
//
#include    <snapdev/callback_manager.h>
#include    <snapdev/timespec_ex.h>



namespace cluck
{



enum class mode_t
{
    CLUCK_MODE_SIMPLE,      // safe code executes without the need for additional asynchronous messages
    CLUCK_MODE_EXTENDED     // safe code requires further messages to be sent/received
};


enum class reason_t
{
    CLUCK_REASON_NONE,
    CLUCK_REASON_LOCAL_TIMEOUT,         // process_timeout() was called
    CLUCK_REASON_REMOTE_TIMEOUT,        // FAILED_LOCK was received with a "timeout" error
    CLUCK_REASON_DEADLOCK,              // FAILED_LOCK was received with a "deadlock" error
    CLUCK_REASON_TRANSMISSION_ERROR,    // communicatord could not forward the message to a cluckd
    CLUCK_REASON_INVALID,               // communicatord did not like our message
};


enum class type_t
{
    CLUCK_TYPE_READ_WRITE,              // wait N seconds after the last READ-ONLY before attempting the READ-WRITE lock
    CLUCK_TYPE_READ_ONLY,               // shared lock
    CLUCK_TYPE_READ_WRITE_PRIORITY,     // prevent further READ-ONLY until done with this lock
};


enum class state_t
{
    CLUCK_STATE_IDLE,
    CLUCK_STATE_LOCKING,
    CLUCK_STATE_LOCKED,
    CLUCK_STATE_UNLOCKING,
    CLUCK_STATE_FAILED,
};


/** \brief A timeout delay.
 *
 * This type is used to defined a timeout delay between an event and its
 * reply. The value represents microseconds.
 *
 * The special value, CLUCK_DEFAULT_TIMEOUT (-1), is used to indicate that
 * the default is to be used.
 */
typedef snapdev::timespec_ex        timeout_t;

inline timeout_t  CLUCK_DEFAULT_TIMEOUT = timeout_t(-1, 0);
inline timeout_t  CLUCK_MINIMUM_TIMEOUT = timeout_t(3, 0);
inline timeout_t  CLUCK_MAXIMUM_TIMEOUT = timeout_t(7 * 24 * 60 * 60, 0);         // no matter what limit all timeouts to this value (7 days)
inline timeout_t  CLUCK_LOCK_OBTENTION_DEFAULT_TIMEOUT = timeout_t(5, 0);
inline timeout_t  CLUCK_LOCK_OBTENTION_MAXIMUM_TIMEOUT = timeout_t(60 * 60, 0);   // by default limit obtention timeout to this value
inline timeout_t  CLUCK_LOCK_DURATION_DEFAULT_TIMEOUT = timeout_t(5, 0);
inline timeout_t  CLUCK_UNLOCK_DEFAULT_TIMEOUT = timeout_t(5, 0);
inline timeout_t  CLUCK_UNLOCK_MINIMUM_TIMEOUT = timeout_t(60, 0);


timeout_t                   get_lock_obtention_timeout();
void                        set_lock_obtention_timeout(timeout_t timeout);
timeout_t                   get_lock_duration_timeout();
void                        set_lock_duration_timeout(timeout_t timeout);
timeout_t                   get_unlock_timeout();
void                        set_unlock_timeout(timeout_t timeout);



/** \brief Cluster lock.
 *
 * This class is used to run code synchronously in a cluster of computers.
 *
 * The class accepts three callbacks:
 *
 * * lock was obtained and is now in place
 * * lock was not obtained (timeout, network error)
 * * finally code (i.e. code to run after the lock was released)
 *
 * The cluck class is itself an eventdispatcher connection so you must add
 * it to the communicator. Actually, you can add it and then forget about the
 * pointer. That way, once you are in your finally code, it automatically
 * gets removed from the communicator.
 */
class cluck
    : public ed::timer
{
public:
    typedef std::shared_ptr<cluck>                  pointer_t;
    typedef std::function<bool(cluck *)>            callback_t;
    typedef snapdev::callback_manager<callback_t>   callback_manager_t;
    typedef std::uint64_t                           serial_t;

                        cluck(
                              std::string const & object_name
                            , ed::connection_with_send_message::pointer_t connection
                            , ed::dispatcher::pointer_t dispatcher
                            , mode_t mode = mode_t::CLUCK_MODE_SIMPLE);
    virtual             ~cluck() override;

    callback_manager_t::callback_id_t
                        add_lock_obtained_callback(
                              callback_t func
                            , callback_manager_t::priority_t priority = callback_manager_t::DEFAULT_PRIORITY);
    bool                remove_lock_obtained_callback(
                              callback_manager_t::callback_id_t id);
    callback_manager_t::callback_id_t
                        add_lock_failed_callback(
                              callback_t func
                            , callback_manager_t::priority_t priority = callback_manager_t::DEFAULT_PRIORITY);
    bool                remove_lock_failed_callback(
                              callback_manager_t::callback_id_t id);
    callback_manager_t::callback_id_t
                        add_finally_callback(
                              callback_t func
                            , callback_manager_t::priority_t priority = callback_manager_t::DEFAULT_PRIORITY);
    bool                remove_finally_callback(
                              callback_manager_t::callback_id_t id);

    timeout_t           get_lock_obtention_timeout() const;
    void                set_lock_obtention_timeout(timeout_t timeout);
    timeout_t           get_lock_duration_timeout() const;
    void                set_lock_duration_timeout(timeout_t timeout);
    timeout_t           get_unlock_timeout() const;
    void                set_unlock_timeout(timeout_t timeout);

    mode_t              get_mode() const;
    type_t              get_type() const;
    void                set_type(type_t type);
    reason_t            get_reason() const;

    bool                lock();
    void                unlock();
    timeout_t           get_timeout_date() const;
    bool                is_locked() const;
    bool                is_busy() const;

    // ed::connection implementation
    virtual void        process_timeout() override;

protected:
    virtual void        lock_obtained();
    virtual void        lock_failed();
    virtual void        finally();

private:
    bool                is_cluck_msg(ed::message & msg) const;
    void                msg_locked(ed::message & msg);
    void                msg_lock_failed(ed::message & msg);
    void                msg_transmission_report(ed::message & msg);
    void                msg_unlocked(ed::message & msg);
    void                msg_unlocking(ed::message & msg);
    void                set_reason(reason_t reason);

    std::string                 f_object_name = std::string();
    ed::dispatcher_match::tag_t const
                                f_tag = ed::dispatcher_match::DISPATCHER_MATCH_NO_TAG;
    ed::connection_with_send_message::pointer_t
                                f_connection = ed::connection_with_send_message::pointer_t();
    ed::dispatcher::pointer_t   f_dispatcher = ed::dispatcher::pointer_t();
    mode_t                      f_mode = mode_t::CLUCK_MODE_SIMPLE;
    callback_manager_t          f_lock_obtained_callbacks = callback_manager_t();
    callback_manager_t          f_lock_failed_callbacks = callback_manager_t();
    callback_manager_t          f_finally_callbacks = callback_manager_t();
    timeout_t                   f_lock_obtention_timeout = CLUCK_DEFAULT_TIMEOUT;
    timeout_t                   f_lock_duration_timeout = CLUCK_DEFAULT_TIMEOUT;
    timeout_t                   f_unlock_timeout = CLUCK_DEFAULT_TIMEOUT;
    timeout_t                   f_lock_timeout_date = timeout_t();
    timeout_t                   f_unlocked_timeout_date = timeout_t();
    type_t                      f_type = type_t::CLUCK_TYPE_READ_WRITE;
    state_t                     f_state = state_t::CLUCK_STATE_IDLE;
    reason_t                    f_reason = reason_t::CLUCK_REASON_NONE;
    serial_t                    f_serial = serial_t();
};



} // namespace cluck
// vim: ts=4 sw=4 et
