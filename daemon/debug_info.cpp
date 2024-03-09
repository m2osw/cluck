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
#include    "debug_info.h"

#include    "cluckd.h"


// last include
//
#include    <snapdev/poison.h>



namespace cluck_daemon
{



/** \class debug_info
 * \brief Handle the SIGUSR2 Unix signal.
 *
 * This class is an implementation of the signalfd() specifically
 * listening for the SIGUSR2 signal.
 *
 * The signal is used to ask cluckd to do some debugging tasks.
 * This is used by developers as they test the tool and it is not
 * actually used in non-debug versions.
 */




/** \brief The debug_info initialization.
 *
 * The debug_info object uses the signalfd() function to
 * obtain a way to listen on incoming Unix signals.
 *
 * Specifically, it listens on the SIGUSR2 signal. This is used to
 * request cluckd to print out debug information. This is for developers
 * to see the low level state of the cluckd service.
 *
 * \param[in] c  The cluckd we are listening for.
 */
debug_info::debug_info(cluckd * c)
    : signal(SIGUSR2)
    , f_cluckd(c)
{
    unblock_signal_on_destruction();
    set_name("dbg_info");
}


/** \brief Call the debug_info() function of the cluckd object.
 *
 * When this function is called, the debug_info() function of
 * cluckd gets called and it can process whatever the programmer
 * wants to debug at the time.
 */
void debug_info::process_signal()
{
#ifdef _DEBUG
    // in debug mode, show the info + some debug data (i.e. tickets)
    //
    f_cluckd->info();

    SNAP_LOG_INFO
        << "++++ serialized tickets in debug_info(): "
        << f_cluckd->serialized_tickets() //.replace("\n", " --- ")
        << SNAP_LOG_SEND;
#else
    SNAP_LOG_INFO
        << "this version of cluckd is not a debug version."
           " The debug_info() function does nothing in this version."
        << SNAP_LOG_SEND;
#endif
}



} // namespace cluck_daemon
// vim: ts=4 sw=4 et
