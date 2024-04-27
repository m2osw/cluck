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
#include    "info.h"

#include    "cluckd.h"


// last include
//
#include    <snapdev/poison.h>



namespace cluck_daemon
{



/** \class info
 * \brief Handle the SIGUSR1 Unix signal.
 *
 * This class is an implementation of the signalfd() specifically
 * listening for the SIGUSR1 signal.
 *
 * The signal is used to ask cluckd to print out information about
 * its current state.
 */




/** \brief The info initialization.
 *
 * The info object uses the signalfd() function to
 * obtain a way to listen on incoming Unix signals.
 *
 * Specifically, it listens on the SIGUSR1 signal. This is used to
 * request cluckd to print out its current state. This is mainly
 * for debug purposes.
 *
 * \param[in] sl  The cluckd we are listening for.
 */
info::info(cluckd * c)
    : signal(SIGUSR1)
    , f_cluckd(c)
{
    unblock_signal_on_destruction();
    set_name("info");
}


/** \brief Call the info function of the cluckd object.
 *
 * When this function is called, the internal state of the cluckd
 * service gets printed out. It is expected to be used to debug the
 * cluckd daemon.
 *
 * This signal can be sent any number of times.
 *
 * Note that state is printed using the log mechanism. If your logs
 * are not being printed in your console, then make sure to look at
 * the cluckd.log file (or whatever you renamed it if you did so
 * in the settings).
 */
void info::process_signal()
{
    f_cluckd->info();
}



} // namespace cluck_daemon
// vim: ts=4 sw=4 et
