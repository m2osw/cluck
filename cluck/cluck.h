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

// self
//
//#include    <cluck/exception.h>


//#include    <eventdispatcher/tcp_bio_client.h>
// eventdispatcher
//
//#include    <eventdispatcher/communicator.h>

//#include    <eventdispatcher/tcp_bio_client.h>


// snapdev
//
#include    <snapdev/callback_manager.h>



namespace cluck
{



/** \brief A timeout delay.
 *
 * This type is used to defined a timeout delay between an event and its
 * reply. The value represents seconds.
 *
 * The special value -1 is used to indicate that the default is to be used.
 */
typedef std::int32_t        timeout_t;

static constexpr timeout_t  CLUCK_DEFAULT_TIMEOUT = -1;
static constexpr timeout_t  CLUCK_MAXIMUM_TIMEOUT = 7 * 24 * 60 * 60;     // no matter what limit all timeouts to this value (7 days)

static constexpr timeout_t  CLUCK_LOCK_DEFAULT_TIMEOUT = 5;
static constexpr timeout_t  CLUCK_UNLOCK_DEFAULT_TIMEOUT = 5;

static constexpr timeout_t  CLUCK_LOCK_MINIMUM_TIMEOUT = 3;
static constexpr timeout_t  CLUCK_LOCK_MAXIMUM_TIMEOUT = 60 * 60;    // by default limit obtention timeout to this value

static constexpr timeout_t  CLUCK_UNLOCK_MINIMUM_TIMEOUT = 60;




void initialize_communicator(
          addr::addr const & addr
        , ed::mode_t mode = ed::mode_t::MODE_PLAIN);



namespace detail
{
class connection;
}


/** \brief Cluster lock.
 *
 * This class is used to run code synchronously in a cluster of computers.
 *
 * You are expected to setup two callbacks to know when the lock is in
 * place and another to know whether an error occurred and the lock was
 * never obtained.
 */
class cluck
{
public:
    typedef std::shared_ptr<cluck>                          pointer_t;
    typedef std::function<bool(std::string const & name)>   callback_t;

                        cluck(std::string const & name);

    void                set_locked_callback(callback_t func);
    void                set_failed_callback(callback_t func);
    void                set_lock_duration(timeout_t duration);
    void                set_obtention_timeout(timeout_t timeout);
    void                set_unlock_timeout(timeout_t timeout);

    bool                lock();
    void                unlock();

    time_t              get_timeout_date() const;
    bool                is_locked() const;
    bool                lock_timed_out() const;

protected:
    virtual bool        lock_obtained();
    virtual bool        lock_failed();

private:
    std::string         f_name = std::string();
    snapdev::callback_manager
                        f_callbacks = snapdev::callback_manager();
    std::shared_ptr<detail::connection>
                        f_connection = std::shared_ptr<detail::connection>();
};



} // namespace cluck
// vim: ts=4 sw=4 et
