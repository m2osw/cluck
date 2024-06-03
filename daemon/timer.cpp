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
#include    "timer.h"

#include    "cluckd.h"


// last include
//
#include    <snapdev/poison.h>



namespace cluck_daemon
{



/** \class timer
 * \brief Handle the locks timeout.
 *
 * This class is used to time out locks. Whenever we receive a new LOCK
 * message or enter a lock the timer is reset with the next lock that is
 * going to time out. When that happens, the cleanup() function gets
 * called. Any lock which timed out is removed and the user on the other
 * end is told about the problem with an UNLOCKING, UNLOCKED or LOCK_FAILED
 * message as the case may be.
 */



/** \brief The timer initialization.
 *
 * The timer is always enabled, however by default there is nothing to
 * timeout. In other words, the timer is keep off.
 *
 * \param[in] c  The cluckd server which will handle time outs.
 */
timer::timer(cluckd * c)
    : ed::timer(-1)
    , f_cluckd(c)
{
    set_name("timer");
}


timer::~timer()
{
}


/** \brief Call the cleanup() function of the cluckd object.
 *
 * A timeout happened, call the cluckd::cleanup() function which takes
 * care of cleaning up the list of lock requests and existing locks that
 * timed out.
 */
void timer::process_timeout()
{
    f_cluckd->cleanup();
}



} // namespace cluck_daemon
// vim: ts=4 sw=4 et
