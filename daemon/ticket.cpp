// Copyright (c) 2016-2025  Made to Order Software Corp.  All Rights Reserved
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
#include    "ticket.h"

#include    "cluckd.h"


// cluck
//
#include    <cluck/exception.h>
#include    <cluck/names.h>


// advgetopt
//
#include    <advgetopt/validator_integer.h>


// snapdev
//
#include    <snapdev/hexadecimal_string.h>
#include    <snapdev/string_replace_many.h>
#include    <snapdev/tokenize_string.h>


// snaplogger
//
#include    <snaplogger/message.h>


// last include
//
#include    <snapdev/poison.h>



namespace cluck_daemon
{



/** \class ticket
 * \brief Handle the ticket messages.
 *
 * \section introduction Introduction
 *
 * This class manages the Leslie Lamport's Bakery Algorithm (1974) lock
 * mechanism (a critical section that we can get between any number
 * of threads, processes, computers.) Details of this algorithm can
 * be found here:
 *
 *   http://en.wikipedia.org/wiki/Lamport's_bakery_algorithm
 *
 * The algorithm requires:
 *
 * \li A unique name for each computer (server_name)
 * \li A unique number for the process attempting the lock
 *     (see gettid(2) manual)
 * \li A user supplied object name (the name of the lock)
 * \li A ticket number (use the largest existing ticket number + 1)
 *
 * We also include a timeout on any one lock so we can forfeit the
 * lock from happening if it cannot be obtained in a minimal amount
 * of time. The timeout is specified as an absolute time in the
 * future (now + X seconds.) The timeout is given in seconds (a
 * standard time_t value).
 *
 * This class sends various messages to manage the locks.
 *
 *
 * \section bakery_algorithm The Bakery Algorithm Explained
 *
 * The bakery algorithm is based on the basic idea that a large number
 * of customers go to one bakery to buy bread. In order to make sure
 * they all are served in the order they come in, they are given a ticket
 * with a number. The ticket numbers increase by one for each new customer.
 * The person still in line with the smallest ticket number is served next.
 * Once served, the ticket is destroyed.
 *
 * \note
 * The ticket numbers can restart at one whenever the queue of customers
 * goes empty. Otherwise it only increases. From our usage in Snap, it is
 * really rare that the ticket numbers would not quickly be reset,
 * especially because we have such numbers on a per object_name basis
 * and thus many times the number will actually be one.
 *
 * On a computer without any synchronization mechanism available (our case)
 * two customers may enter the bakery simultaneously (especially since we
 * are working with processes that may run on different computers.) This
 * means two customers may end up with the exact same ticket number and
 * there are no real means to avoid that problem. However, each customer
 * is also assigned two unique numbers on creation: its "host number"
 * (its server name, we use a string to simplify things) and its process
 * number (we actually use gettid() so each thread gets a unique number
 * which is an equivalent to a pid_t number for every single thread.)
 * These two numbers are used to further order processes and make sure
 * we can tell who will get the lock first.
 *
 * So, the basic bakery algorithm looks like this in C++. This algorithm
 * expects memory to be guarded (shared or "volatile"; always visible by
 * all threads.) In our case, we send the data over the network to
 * all the snaplock processes. This is definitely guarded.
 *
 * \code
 *     // declaration and initial values of global variables
 *     namespace {
 *         int                   num_threads = 100;
 *         std::vector<bool>     entering;
 *         std::vector<uint32_t> tickets;
 *     }
 *
 *     // initialize the vectors
 *     void init()
 *     {
 *         entering.reserve(num_threads);
 *         tickets.reserve(num_threads);
 *     }
 *
 *     // i is a thread "number" (0 to 99)
 *     void lock(int i)
 *     {
 *         // get the next ticket
 *         entering[i] = true;
 *         int my_ticket(0);
 *         for(int j(0); j < num_threads; ++j)
 *         {
 *             if(ticket[k] > my_ticket)
 *             {
 *                 my_ticket = ticket[k];
 *             }
 *         }
 *         ++my_ticket; // add 1, we want the next ticket
 *         entering[i] = false;
 *
 *         for(int j(0); j < num_threads; ++j)
 *         {
 *             // wait until thread j receives its ticket number
 *             while(entering[j])
 *             {
 *                 sleep();
 *             }
 *
 *             // there are several cases:
 *             //
 *             // (1) tickets that are 0 are not assigned so we can just go
 *             //     through
 *             //
 *             // (2) smaller tickets win over us (have a higher priority,)
 *             //     so if there is another thread with a smaller ticket
 *             //     sleep a little and try again; that ticket must go to
 *             //     zero to let us through that guard
 *             //
 *             // (3) if tickets are equal, compare the thread numbers and
 *             //     like the tickets, the smallest thread wins
 *             //
 *             while(ticket[j] != 0 && (ticket[j] < ticket[i] || (ticket[j] == ticket[i] && j < i))
 *             {
 *                 sleep();
 *             }
 *         }
 *     }
 *     
 *     // i is the thread number
 *     void unlock(int i)
 *     {
 *         // release our ticket
 *         ticket[i] = 0;
 *     }
 *   
 *     void SomeThread(int i)
 *     {
 *         while(true)
 *         {
 *             [...]
 *             // non-critical section...
 *             lock(i);
 *             // The critical section code goes here...
 *             unlock(i);
 *             // non-critical section...
 *             [...]
 *         }
 *     }
 * \endcode
 *
 * Note that there are two possible optimizations when actually
 * implementing the algorithm:
 *
 * \li You can enter (entering[i] = true), get your ticket,
 *     exit (entering[i] = false) and then get the list of
 *     still existing 'entering' processes. Once that list
 *     goes empty, we do not need to test the entering[j]
 *     anymore because any further entering[j] will be about
 *     processes with a larger ticket number and thus
 *     processes that will appear later in the list of tickets.
 *
 * \li By sorting (and they are) our ticket requests by ticket,
 *     server name, and process pid, we do not have to search
 *     for the smallest ticket. The smallest ticket is automatically
 *     first in that list! So all we have to do is: if not first,
 *     sleep() some more.
 *
 * \section implementation Our implementation in cluck
 *
 * Locks are given a name by our users. This is used to lock just
 * one small thing for any amount of time as required by your
 * implementation.
 *
 * That name is used as an index to the f_tickets object in the
 * snaplock class. Within such a ticket, you have one entry per
 * process trying to obtain that lock.
 *
 * For example, the users plugin generates a unique user identifier
 * which is a number starting at 1. When a process needs to do this,
 * we need a lock to prevent any other processes to do it at the
 * same time. We also use a QUORUM consistency in Cassandra to
 * load/increment/save the user number.
 *
 * In this example, all we need to lock is an object named something
 * like "user number". Actually, if the number is specific to a
 * website, we can use the website URI. In this case, we can use a
 * name like this: "http://www.example.com/user#number". This says
 * we are managing an atomic "#number" at address
 * "http://www.example.com/user". This also means we do not need
 * to block anyone if the other people need to lock a completely
 * different field (so process A can lock the user unique number
 * while process B could lock an invoice unique number.)
 *
 * As a result, the locking mechanism manages the locks on a per
 * lock name basis. In other words, if only two processes request
 * a lock simultaneously and the object_name parameter are not equal,
 * they both get their lock instantaneously (at least very quickly.)
 *
 * \subsection message_sequence Message Sequence Chart
 *
 * \msc
 *  Client,SnapLockA,SnapLockB,SnapLockC;
 *
 *  Client->SnapLockA [label="LOCK"];
 *
 *  SnapLockA->SnapLockA [label="LOCK_ENTERING"];
 *  SnapLockA->SnapLockB [label="LOCK_ENTERING"];
 *  SnapLockA->SnapLockC [label="LOCK_ENTERING"];
 *
 *  SnapLockA->SnapLockA [label="LOCK_ENTERED"];
 *  SnapLockB->SnapLockA [label="LOCK_ENTERED"];
 *  SnapLockC->SnapLockA [label="LOCK_ENTERED"];
 *
 *  SnapLockA->SnapLockA [label="GET_MAX_TICKET"];
 *  SnapLockA->SnapLockB [label="GET_MAX_TICKET"];
 *  SnapLockA->SnapLockC [label="GET_MAX_TICKET"];
 *
 *  SnapLockA->SnapLockA [label="MAX_TICKET"];
 *  SnapLockB->SnapLockA [label="MAX_TICKET"];
 *  SnapLockC->SnapLockA [label="MAX_TICKET"];
 *
 *  SnapLockA->SnapLockA [label="ADD_TICKET"];
 *  SnapLockA->SnapLockB [label="ADD_TICKET"];
 *  SnapLockA->SnapLockC [label="ADD_TICKET"];
 *
 *  SnapLockA->SnapLockA [label="TICKET_ADDED"];
 *  SnapLockB->SnapLockA [label="TICKET_ADDED"];
 *  SnapLockC->SnapLockA [label="TICKET_ADDED"];
 *
 *  SnapLockA->SnapLockA [label="LOCK_EXITING"];
 *  SnapLockA->SnapLockB [label="LOCK_EXITING"];
 *  SnapLockA->SnapLockC [label="LOCK_EXITING"];
 *
 *  SnapLockA->Client [label="LOCKED"];
 * \endmsc
 *
 *
 * \section drawback Any drawback?
 *
 * \subsection timeouts Timeouts
 *
 * All our locks come with a timeout. The default is defined in
 * CLUCK_LOCK_DURATION_DEFAULT_TIMEOUT, which is 5 seconds.
 * (5 seconds, which for a front end hit to a website is very
 * long already!) If that timeout is too short (i.e. a backend
 * does heavy lifting work on the data), then you can make it
 * larger. Our backends are given 4h by default.
 *
 * \subsection deadlock Deadlock
 *
 * Like with any lock, if you have two processes that both try
 * two distinct locks each in the other order, you get a deadlock:
 *
 * P1 tries to get L1, and gets it;
 *
 * P2 tries to get L2, and gets it;
 *
 * P1 tries to get L2, and has to wait on P2;
 *
 * P2 tries to get L1, and creates a deadlock.
 *
 * The deadlock itself will be resolved once a lock times out,
 * but P2 will "never" have a chance to work on L1 if that sequence
 * always happens.
 */




/** \brief Initialize a ticket object.
 *
 * The constructor initializes a ticket object by creating a ticket
 * key and allocating an entering object.
 *
 * Once the entering object was acknowledged by QUORUM cluck daemon
 * instances (i.e. one other computer since we allow exactly 3 leaders,)
 * we can then create the ticket.
 *
 * \note
 * We create a key from the server name, client PID, and object
 * name for the entering process to run. This key is unique
 * among all computers assuming (1) your client PID is unique and
 * (2) your servers all have unique names and both of these conditions
 * are always true (i.e. we do not allow a cluckd to join a cluster if
 * its name was already registered).
 *
 * \note
 * If you use threads, or are likely to use threads, make sure to
 * use the gettid() function instead of getpid() to define a
 * unique client PID. (Note: this is done in the cluck library.)
 *
 * \param[in] c  A pointer to the cluckd object.
 * \param[in] messenger  A pointer to the messenger.
 * \param[in] object_name  The name of the object getting locked.
 * \param[in] entering_key  The key (ticket) used to entery the bakery.
 * \param[in] obtension_timeout  The time when the attempt to get the lock
 *                               times out in seconds.
 * \param[in] lock_duration  The amount of time the lock lasts once obtained.
 * \param[in] server_name  The name of the server generating the locked.
 * \param[in] service_name  The service waiting for the LOCKED message.
 */
ticket::ticket(
              cluckd * c
            , messenger::pointer_t messenger
            , std::string const & object_name
            , ed::dispatcher_match::tag_t tag
            , std::string const & entering_key
            , cluck::timeout_t obtention_timeout
            , cluck::timeout_t lock_duration
            , std::string const & server_name
            , std::string const & service_name)
    : f_cluckd(c)
    , f_messenger(messenger)
    , f_object_name(object_name)
    , f_tag(tag)
    , f_obtention_timeout(obtention_timeout)
    , f_lock_duration(std::clamp(
              lock_duration
            , cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT
            , cluck::CLUCK_MAXIMUM_TIMEOUT))
    , f_server_name(server_name)
    , f_service_name(service_name)
    , f_owner(f_cluckd->get_server_name())
    , f_entering_key(entering_key)
{
    set_unlock_duration(f_lock_duration);

    // TODO: see how to not say "attempting a lock" when we are deserializing
    //       an existing lock.
    SNAP_LOG_TRACE
        << "Attempting to lock \""
        << f_object_name
        << "\" ("
        << f_tag
        << ") on \""
        << f_entering_key
        << "\" for \""
        << f_server_name
        << '/'
        << f_service_name
        << "\" (timeout: "
        << f_obtention_timeout
        << ")."
        << SNAP_LOG_SEND;
}


/** \brief Send a message to the other two leaders.
 *
 * The \p msg is "broadcast" to the other two leaders.
 *
 * This is a safe guard so if one of our three leaders fails, we have
 * a backup of the lock status.
 *
 * The locking system also works if there are only two or even just one
 * computer. In those cases, special care has to be taken to get things
 * to work as expected.
 *
 * \param[in] msg  The message to send to the other two leaders.
 *
 * \return true if the message was forwarded at least once, false otherwise.
 */
bool ticket::send_message_to_leaders(ed::message & msg)
{
    // finish the message initialization
    //
    msg.set_service(cluck::g_name_cluck_service_name);
    msg.add_parameter(cluck::g_name_cluck_param_object_name, f_object_name);
    msg.add_parameter(cluck::g_name_cluck_param_tag, f_tag);

    computer::pointer_t leader(f_cluckd->get_leader_a());
    if(leader != nullptr)
    {
        // there are at least two leaders
        //
        int count(0);
        msg.set_server(leader->get_name());
        if(f_messenger->send_message(msg))
        {
            ++count;
        }

        // check for a third leader
        //
        leader = f_cluckd->get_leader_b();
        if(leader != nullptr)
        {
            msg.set_server(leader->get_name());
            if(f_messenger->send_message(msg))
            {
                ++count;
            }
        }

        // we have to wait for at least one reply if we were able to send
        // at least one message
        //
        return count > 0;
    }

    // there is only one leader (ourselves)
    //
    // call the one_leader() function to verify that this is indeed correct
    // otherwise we would mess up the algorithm
    //
    return one_leader();
}


/** \brief Enter the mode that lets us retrieve our ticket number.
 *
 * In order to make sure we can get the current largest ticket number
 * in a unique enough way, cluck has to enter the lock loop. This
 * process starts by sending a `LOCK_ENTERING` message to all the
 * other cluckd leaders.
 */
void ticket::entering()
{
    // TODO implement the special case when there is only 1 leader
    //      (on the other hand, that should be rather rare)
    //computer::pointer_t leader(f_cluckd->get_leader_a());
    //if(leader == nullptr)
    //{
    //    -- do the necessary to obtain the lock --
    //    return;
    //}

    ed::message entering_message;
    entering_message.set_command(cluck::g_name_cluck_cmd_lock_entering);
    entering_message.add_parameter(cluck::g_name_cluck_param_key, f_entering_key);
    entering_message.add_parameter(cluck::g_name_cluck_param_timeout, f_obtention_timeout);
    entering_message.add_parameter(cluck::g_name_cluck_param_duration, f_lock_duration);
    if(f_lock_duration != f_unlock_duration)
    {
        entering_message.add_parameter(cluck::g_name_cluck_param_unlock_duration, f_unlock_duration);
    }
    entering_message.add_parameter(cluck::g_name_cluck_param_source, f_server_name + "/" + f_service_name);
    entering_message.add_parameter(cluck::g_name_cluck_param_serial, f_serial);
    if(send_message_to_leaders(entering_message))
    {
        if(one_leader())
        {
            // there are no other leaders, make sure the algorithm progresses
            //
            entered();
        }
    }
}


/** \brief Tell this entering that we received a LOCKENTERED message.
 *
 * This function gets called each time we receive a `LOCKENTERED`
 * message with this ticket entering key.
 *
 * Since we have 1 to 3 leaders, the quorum and thus consensus is reached
 * as soon as we receive one `LOCKENTERED` message. So as a result this
 * function sends `GETMAXTICKET` the first time it gets called. The
 * `GETMAXTICKET` message allows us to determine the ticket number for
 * the concerned object.
 *
 * \note
 * The msg_lockentered() function first checked whether the
 * `LOCKENTERED` message had anything to do with this ticket.
 * If not, the message was just ignored.
 */
void ticket::entered()
{
    // is this ticket concerned?
    //
    if(!f_get_max_ticket)
    {
        // with 2 or 3 leaders, quorum is obtain with one
        // single acknowledgement
        //
        f_get_max_ticket = true;

        // calculate this instance max. ticket number
        //
        f_our_ticket = f_cluckd->get_last_ticket(f_object_name);

        ed::message get_max_ticket_message;
        get_max_ticket_message.set_command(cluck::g_name_cluck_cmd_get_max_ticket);
        get_max_ticket_message.add_parameter(cluck::g_name_cluck_param_key, f_entering_key);
        if(send_message_to_leaders(get_max_ticket_message))
        {
            if(one_leader())
            {
                // there are no other leaders, make sure the algorithm progresses
                //
                max_ticket(f_our_ticket);
            }
        }
    }
}


/** \brief Called whenever a MAX_TICKET is received.
 *
 * This function registers the largest ticket number. Once we reach
 * QUORUM, then we have the largest number and we can move on to the
 * next stage, which is to add the ticket.
 *
 * \note
 * We reach quorum immediately in our current implementation since we
 * have 1, 2, or 3 leaders. So this function takes the input in account
 * once, calls add_ticket() immediately and if the 3rd leader does send
 * a reply too, it gets ignored.
 *
 * \param[in] new_max_ticket  Another possibly larger ticket.
 */
void ticket::max_ticket(ticket_id_t new_max_ticket)
{
    if(!f_added_ticket)
    {
        if(new_max_ticket > f_our_ticket)
        {
            f_our_ticket = new_max_ticket;
        }

        ++f_our_ticket;
        if(f_our_ticket == NO_TICKET)
        {
            // f_out_ticket is a 32 bit number, this can happen only if you
            // created over 4 billion locks back to back--i.e. created a new
            // one before the previous one was released; or put in a different
            // way: the list of tickets with that "object name" never went
            // back to being empty for that long...
            //
            throw cluck::out_of_range("ticket::max_ticket() tried to generate the next ticket and got a wrapping around number.");
        }

        add_ticket();
    }
}


/** \brief Send the ADD_TICKET message.
 *
 * This function sends the ADD_TICKET message to all the cluckd
 * instances currently known.
 *
 * \exception logic_error
 * This exception is raised if the function gets called twice or more.
 * Since it is considered an internal function, it should not be an issue.
 */
void ticket::add_ticket()
{
    // we expect exactly one call to this function
    //
    if(f_added_ticket)
    {
        throw cluck::logic_error("ticket::add_ticket() called more than once."); // LCOV_EXCL_LINE
    }
    f_added_ticket = true;

    //
    // WARNING: the ticket key MUST be properly sorted by:
    //
    //              ticket number
    //              server name
    //              client pid
    //
    // The client PID does not need to be sorted numerically, just be sorted
    // so one client is before the other.
    //
    // However, the ticket number MUST be numerically sorted. For this reason,
    // since the key is a string, we must add introducing zeroes.
    //
    f_ticket_key = snapdev::int_to_hex(f_our_ticket, false, 8)
                 + '/'
                 + f_entering_key;

    f_cluckd->set_ticket(f_object_name, f_ticket_key, shared_from_this());

    ed::message add_ticket_message;
    add_ticket_message.set_command(cluck::g_name_cluck_cmd_add_ticket);
    add_ticket_message.add_parameter(cluck::g_name_cluck_param_key, f_ticket_key);
    add_ticket_message.add_parameter(cluck::g_name_cluck_param_timeout, f_obtention_timeout);
    if(send_message_to_leaders(add_ticket_message))
    {
        if(one_leader())
        {
            ticket_added(f_cluckd->get_entering_tickets(f_object_name));
        }
    }
}


/** \brief Called whenever a TICKET_ADDED is received.
 *
 * This function sends a LOCK_EXITING if the ticket reached the total number
 * of TICKET_ADDED required to get a quorum (which is just one with 1 to 3
 * leaders.)
 *
 * The \p still_entering paramater defines the list of tickets that are
 * still trying to enter the same object. This is very important. It needs
 * to be completely drained before we can proceed and mark the ticket as
 * assigned.
 *
 * \param[in] still_entering  The list of still entering processes
 */
void ticket::ticket_added(key_map_t const & still_entering)
{
    if(!f_added_ticket_quorum)
    {
        // when we have 2 or 3 leaders, quorum is obtain with one
        // single acknowledgement
        //
        f_added_ticket_quorum = true;

        f_still_entering = still_entering;

        // okay, the ticket was added on all cluck daemons
        // now we can forget about the entering flag
        // (equivalent to setting it to false)
        //
        ed::message exiting_message;
        exiting_message.set_command(cluck::g_name_cluck_cmd_lock_exiting);
        exiting_message.add_parameter(cluck::g_name_cluck_param_key, f_entering_key);
        snapdev::NOT_USED(send_message_to_leaders(exiting_message));

        f_cluckd->lock_exiting(exiting_message);
    }
}


/** \brief Call any time time an entering flag is reset.
 *
 * This function gets called whenever an entering flag gets set
 * back to false (i.e. removed in our implementation).
 *
 * This function knows whether this ticket received its number
 * and is not yet ready. In both of these circumstances, we
 * are waiting for all entering flags that got created while
 * we determined the largest ticket number to be removed.
 *
 * \param[in] key  The key of the ticket that was entered.
 */
void ticket::remove_entering(std::string const & key)
{
    if(f_added_ticket_quorum
    && !f_ticket_ready)
    {
        auto it(f_still_entering.find(key));
        if(it != f_still_entering.end())
        {
            f_still_entering.erase(it);

            // just like the quorum computation, we compute the
            // remaining list of entering tickets dynamically at
            // the time we check the value
            //
            for(auto key_entering(f_still_entering.begin()); key_entering != f_still_entering.end(); )
            {
                if(key_entering->second->timed_out())
                {
                    key_entering = f_still_entering.erase(key_entering);
                }
                else
                {
                    ++key_entering;
                }
            }

            // once all removed, our ticket is ready!
            //
            if(f_still_entering.empty())
            {
                f_ticket_ready = true;

                // let the other two leaders know that the ticket is ready
                //
                ed::message ticket_ready_message;
                ticket_ready_message.set_command(cluck::g_name_cluck_cmd_ticket_ready);
                ticket_ready_message.add_parameter(cluck::g_name_cluck_param_key, f_ticket_key);
                snapdev::NOT_USED(send_message_to_leaders(ticket_ready_message));
            }
        }
    }
}


/** \brief Check whether this ticket can be activated and do so if so.
 *
 * This function checks whether the ticket is ready to be activated.
 * This means it got a ticket and the ticket is ready. If so, then
 * it sends the LOCKED message back to the system that required it.
 *
 * This function can be called multiple times. It will send
 * the ACTIVATE_LOCK message only once.
 *
 * On a system with only one computer, it will also send the LOCKED
 * message immediately.
 */
void ticket::activate_lock()
{
    if(f_ticket_ready
    && !f_locked
    && f_lock_failed == lock_failure_t::LOCK_FAILURE_NONE)
    {
        ed::message activate_lock_message;
        activate_lock_message.set_command(cluck::g_name_cluck_cmd_activate_lock);
        activate_lock_message.add_parameter(cluck::g_name_cluck_param_key, f_ticket_key);
        if(send_message_to_leaders(activate_lock_message))
        {
            if(one_leader())
            {
                lock_activated();
            }
        }
    }
}


/** \brief Check whether this ticket can be activated and do so if so.
 *
 * This function checks whether the ticket is ready to be activated.
 * This means it got a ticket and the ticket is ready. If so, then
 * it sends the LOCKED message back to the system that required it.
 *
 * This function can be called multiple times. It will send
 * the LOCKED message only once.
 */
void ticket::lock_activated()
{
    if(f_ticket_ready
    && !f_locked
    && f_lock_failed == lock_failure_t::LOCK_FAILURE_NONE)
    {
        f_locked = true;
        f_lock_timeout_date = snapdev::now() + f_lock_duration;
        f_unlocked_timeout_date = f_lock_timeout_date + f_unlock_duration;

        if(f_owner == f_cluckd->get_server_name())
        {
            ed::message locked_message;
            locked_message.set_command(cluck::g_name_cluck_cmd_locked);
            locked_message.set_server(f_server_name);
            locked_message.set_service(f_service_name);
            locked_message.add_parameter(cluck::g_name_cluck_param_object_name, f_object_name);
            locked_message.add_parameter(cluck::g_name_cluck_param_timeout_date, f_lock_timeout_date);
            locked_message.add_parameter(cluck::g_name_cluck_param_unlocked_date, f_unlocked_timeout_date);
            locked_message.add_parameter(cluck::g_name_cluck_param_tag, f_tag);
            f_messenger->send_message(locked_message);
        }
    }
}


/** \brief We are done with the ticket.
 *
 * This function sends the DROP_TICKET message to get rid of a ticket
 * from another leader's list of tickets.
 *
 * Another leader has a list of tickets as it receives LOCK and ADDTICKET
 * messages.
 */
void ticket::drop_ticket()
{
    SNAP_LOG_TRACE
        << "Unlock on \""
        << f_object_name
        << "\" ("
        << f_tag
        << ") with key \""
        << f_entering_key
        << "\"."
        << SNAP_LOG_SEND;

    ed::message drop_ticket_message;
    drop_ticket_message.set_command(cluck::g_name_cluck_cmd_drop_ticket);
    drop_ticket_message.add_parameter(
              cluck::g_name_cluck_param_key
            , f_ticket_key.empty() ? f_entering_key : f_ticket_key);
    send_message_to_leaders(drop_ticket_message);

    if(f_lock_failed == lock_failure_t::LOCK_FAILURE_NONE)
    {
        f_lock_failed = lock_failure_t::LOCK_FAILURE_UNLOCKING;

        //if(f_owner == f_cluckd->get_server_name()) -- this can happen with any leader so we have to send the UNLOCKED
        //                                              the other leaders won't call this function they receive DROP_TICKET
        //                                              instead and as mentioned in the TODO below, we should get a QUORUM
        //                                              instead...
        {
            // we can immediately say it got unlocked...
            //
            // TODO: this is true ONLY if you lock the same object no more than
            //       once within a session, which is not unlikely false (it is
            //       true for what I can remember of Snap!, but long term this
            //       is not safe.) Like the LOCK, we need a quorum and then
            //       send the UNLOCK... At this point, I'm not too sure how
            //       we implement such because the drop_ticket function ends
            //       up deleting the ticket from memory and thus no counting
            //       can happen after that... (i.e. we need a special case
            //       of the receiver for the UNLOCK, argh!)
            //
            ed::message unlocked_message;
            unlocked_message.set_command(cluck::g_name_cluck_cmd_unlocked);
            unlocked_message.set_server(f_server_name);
            unlocked_message.set_service(f_service_name);
            unlocked_message.add_parameter(cluck::g_name_cluck_param_object_name, f_object_name);
            unlocked_message.add_parameter(cluck::g_name_cluck_param_unlocked_date, snapdev::now());
            unlocked_message.add_parameter(cluck::g_name_cluck_param_tag, f_tag);
            f_messenger->send_message(unlocked_message);
        }
    }
}


/** \brief Let the service that wanted this lock know that it failed.
 *
 * This function sends a reply to the server that requested the lock to
 * let it know that it somehow failed.
 *
 * The function replies with a LOCK_FAILED when the lock was never
 * obtained. In this case the origin server cannot access the resources.
 *
 * The function replies with UNLOCKING when the lock timed out. The
 * server is expected to send an UNLOCK reply to acknowledge the
 * failure and fully release the lock. The lock will remain in place
 * until that acknowledgement is received or an amount of time
 * equal to the lock duration by default with a minimum of 1 minute.
 *
 * The UNLOCKING acknowledgement timeout is set to the same amount as
 * the LOCK duration if the `unlock_duration` parameter is not specified
 * in the LOCK message. When the `unlock_duration` parameter is specified,
 * then that amount is used instead.
 *
 * \note
 * The function may get called multiple times. The failure message
 * is sent only on the first call.
 *
 * \note
 * If the ticket was created on another cluck daemon (not the one that
 * received the LOCK event in the first place) then this ticket is
 * not marked as being owned by this cluck daemon and as a result this
 * function only marks the ticket as failed.
 *
 * \param[in] reason  A reason for the failure (i.e. "timed out")
 */
void ticket::lock_failed(std::string const & reason)
{
    enum send_msg_t
    {
        SEND_MSG_NONE,
        SEND_MSG_UNLOCKING,
        SEND_MSG_UNLOCKED,
        SEND_MSG_FAILED,
    };

    send_msg_t send(SEND_MSG_NONE);

    switch(f_lock_failed)
    {
    case lock_failure_t::LOCK_FAILURE_NONE:
        // send that message at most once
        //
        f_lock_failed = lock_failure_t::LOCK_FAILURE_LOCK;

        if(f_locked)
        {
            // now we have to extend the lock timeout to make sure that
            // the UNLOCKING has a chance to be acknowledged
            //
            f_lock_timeout_date += f_unlock_duration;
            if(timed_out())
            {
                // this case is logical here, but I don't think it can
                // happen because the f_locked is true and thus the only
                // value we can use is f_lock_timeout_date and we just
                // increased that value by at least 3 seconds
                //
                send = SEND_MSG_UNLOCKED; // LCOV_EXCL_LINE
            }
            else
            {
                send = SEND_MSG_UNLOCKING;
            }
        }
        else
        {
            send = SEND_MSG_FAILED;
        }
        break;

    case lock_failure_t::LOCK_FAILURE_LOCK:
        f_lock_failed = lock_failure_t::LOCK_FAILURE_UNLOCKING;

        if(f_locked)
        {
            send = SEND_MSG_UNLOCKED;
        }
        break;

    case lock_failure_t::LOCK_FAILURE_UNLOCKING:
        // we already sent all the possible messages
        break;

    }

    // we want the f_lock_failed and f_lock_timeout_date set before returning
    //
    if(f_owner != f_cluckd->get_server_name())
    {
        return;
    }

    switch(send)
    {
    case SEND_MSG_NONE:
        // don't send another message
        break;

    case SEND_MSG_UNLOCKING:
        {
            // if we were locked and reach here, then the lock
            // timed out while locked but the unlock timeout was
            // not yet reached so just send an UNLOCKING message
            //
            SNAP_LOG_IMPORTANT
                << "Lock on \""
                << f_object_name
                << "\" ("
                << f_tag
                << ") with key \""
                << f_entering_key
                << "\" timed out its lock allowed time."
                << SNAP_LOG_SEND;

            ed::message lock_failed_message;
            lock_failed_message.set_command(cluck::g_name_cluck_cmd_unlocking);
            lock_failed_message.set_server(f_server_name);
            lock_failed_message.set_service(f_service_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, f_object_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, f_tag);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_timedout);
            f_messenger->send_message(lock_failed_message);
        }
        break;

    case SEND_MSG_UNLOCKED:
        {
            // if we were locked and/or unlocking and we reach here,
            // then the lock completely timed out and we immediately
            // completely unlock with an UNLOCKED message
            //
            // IMPORTANT: that means the service should stop using the
            //            shared resources but there is absoltely no
            //            guarantee about that; however, this situation
            //            should only occur when a service somehow does
            //            not properly UNLOCK its lock
            //
            SNAP_LOG_IMPORTANT
                << "Lock on \""
                << f_object_name
                << "\" ("
                << f_tag
                << ") with key \""
                << f_entering_key
                << "\" timed out its unlocking allowed time."
                << SNAP_LOG_SEND;

            ed::message lock_failed_message;
            lock_failed_message.set_command(cluck::g_name_cluck_cmd_unlocked);
            lock_failed_message.set_server(f_server_name);
            lock_failed_message.set_service(f_service_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, f_object_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, f_tag);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_timedout);
            f_messenger->send_message(lock_failed_message);
        }
        break;

    case SEND_MSG_FAILED:
        {
            SNAP_LOG_IMPORTANT
                << "Lock on \""
                << f_object_name
                << "\" ("
                << f_tag
                << ") with key \""
                << f_entering_key
                << "\" failed."
                << SNAP_LOG_SEND;

            ed::message lock_failed_message;
            lock_failed_message.set_command(cluck::g_name_cluck_cmd_lock_failed);
            lock_failed_message.set_server(f_server_name);
            lock_failed_message.set_service(f_service_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_object_name, f_object_name);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_tag, f_tag);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_key, f_entering_key);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_error, cluck::g_name_cluck_value_failed);
            lock_failed_message.add_parameter(cluck::g_name_cluck_param_description,
                    "ticket failed before or after the lock was obtained ("
                    + reason
                    + ")");
            f_messenger->send_message(lock_failed_message);
        }
        break;

    }
}


/** \brief Define whether this ticket is the owner of that lock.
 *
 * Whenever comes time to send the LOCK, UNLOCK, or LOCK_FAILED messages,
 * only the owner is expected to send it. This flag tells us who the
 * owner is and thus who is responsible for sending that message.
 *
 * \todo
 * The ownership has to travel to others whenever a leader disappears.
 *
 * \param[in] owner  The name of this ticket owner.
 */
void ticket::set_owner(std::string const & owner)
{
    f_owner = owner;
}


/** \brief Return the name of this ticket's owner.
 *
 * This function returns the name of the owner of this ticket. When a
 * leader dies out, its name stick around until a new leader gets
 * assigned to it.
 *
 * The owner is actually the name of the sending server. So if leader 1
 * is named "alfred" and it sends a ticket message (i.e. LOCK_ENTERING),
 * then the ticket owner parameter will be set  "alfred".
 *
 * The owner name is set when you create a ticket or by unserializing
 * a ticket dump. Serialization is used to share tickets between
 * cluck daemon when we lose a leader and a new computer becomes a
 * new leader.
 *
 * \return  The name of this ticket owner.
 */
std::string const & ticket::get_owner() const
{
    return f_owner;
}


/** \brief Retrieve the client process identifier.
 *
 * This function splits the entering key and return the process identifier.
 * This is primarily used to resend a LOCK message since in most cases
 * this information should not be required.
 *
 * \note
 * This is not really information that the ticket is supposed to know about
 * but well... there is now a case where we need to know this.
 *
 * \return The process identifier of this ticket owner.
 */
pid_t ticket::get_client_pid() const
{
    std::vector<std::string> segments;
    if(snapdev::tokenize_string(segments, f_entering_key, "/") != 2)
    {
        throw cluck::invalid_parameter(
                  "ticket::get_client_pid() split f_entering_key \""
                + f_entering_key
                + "\" and did not get exactly two segments.");
    }
    std::int64_t value;
    advgetopt::validator_integer::convert_string(segments[1], value);
    return static_cast<pid_t>(value);
}


/** \brief Give the lock a serial number for some form of unicity.
 *
 * When we lose a leader, the unicity of the ticket may be required as we
 * start sharing the tickets between the surviving leaders. This is done
 * for the RELOCK message which attempts to restart the an old LOCK. In
 * that case, two leaders end up attempt a RELOCK on the same ticket.
 * To make sure that we can easily ignore the second attempt, we use
 * the serial number to see that the exact same message is getting there
 * twice.
 *
 * The cluck daemon uses the leader number as part of the serial
 * number (bits 24 and 25) so it is unique among all the instances,
 * at least until a cluck deamon dies and its unique numbers get
 * mingled (and the old leaders may change their own number too...)
 *
 * \param[in] serial  The serial number of the ticket.
 */
void ticket::set_serial(serial_t serial)
{
    f_serial = serial;
}


/** \brief Return the serial number of this ticket.
 *
 * This function returns the serial number of this ticket. See the
 * set_serial() function for additional information about this number.
 *
 * \return  The serial number of the ticket.
 */
ticket::serial_t ticket::get_serial() const
{
    return f_serial;
}


/** \brief Change the unlock duration to the specified value.
 *
 * If the service requesting a lock fails to acknowledge an unlock, then
 * the lock still gets unlocked after this \p duration.
 *
 * By default, this parameter gets set to the same value as duration with
 * a minimum of 3 seconds. When the message includes an `unlock_duration`
 * parameter then that value is used instead.
 *
 * \note
 * If \p duration is less than cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT,
 * then cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT is used. At time of writing
 * cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT is 3 seconds.
 *
 * \warning
 * It is important to understand that as soon as an UNLOCKED event arrives,
 * you should acknowledge it. Not doing so increases the risk that two or
 * more processes access the same resource simultaneously.
 *
 * \param[in] duration  The amount of time to acknowledge an UNLOCKED
 *            event; after that the lock is released no matter what.
 */
void ticket::set_unlock_duration(cluck::timeout_t duration)
{
    if(duration == cluck::CLUCK_DEFAULT_TIMEOUT)
    {
        duration = f_lock_duration;
    }

    f_unlock_duration = std::clamp(
                              duration
                            , cluck::CLUCK_UNLOCK_MINIMUM_TIMEOUT
                            , cluck::CLUCK_MAXIMUM_TIMEOUT);
}


/** \brief Get unlock duration.
 *
 * The unlock duration is used in case the lock times out. It extends
 * the lock duration for that much longer until the client acknowledge
 * the locks or the lock really times out.
 *
 * \note
 * If not yet set, this function returns zero (a null timestamp).
 *
 * \return The unlock acknowledgement timeout duration.
 */
cluck::timeout_t ticket::get_unlock_duration() const
{
    return f_unlock_duration;
}


/** \brief Mark the ticket as being ready.
 *
 * This ticket is marked as being ready.
 *
 * A ticket is ready when all the entering tickets were removed from it
 * on the owning leader. On the other two leaders, the ticket gets marked
 * as being ready once they receive the LOCKEXITING message.
 */
void ticket::set_ready()
{
    f_ticket_ready = true;
}


/** \brief Set the ticket number.
 *
 * The other two leaders receive the ticket number in the ADDTICKET
 * message. That number must be saved in the ticket, somehow. This
 * is the function we use to do that.
 *
 * It is very important to have the correct number (by default it is
 * zero) since the algorithm asks for the maximum ticket number
 * currently available and without that information that request
 * cannot be answered properly.
 *
 * \param[in] number  The ticket number to save in f_our_ticket.
 */
void ticket::set_ticket_number(ticket_id_t const number)
{
    if(f_our_ticket != NO_TICKET
    || f_added_ticket)
    {
        throw cluck::logic_error("ticket::set_ticket_number() called with "
                + std::to_string(number)
                + " when f_our_ticket is already set to "
                + std::to_string(f_our_ticket)
                + ".");
    }
    f_added_ticket = true;

    f_our_ticket = number;
    f_ticket_key = snapdev::int_to_hex(f_our_ticket, false, 8)
                 + '/'
                 + f_entering_key;
}


/** \brief Return the ticket number of this ticket.
 *
 * This function returns the ticket number of this ticket. This
 * is generally used to determine the largest ticket number
 * currently in use in order to attach a new ticket number
 * to a lock object.
 *
 * By default the value is NO_TICKET meaning that no ticket number was
 * yet assigned to that ticket object.
 *
 * \return The current ticket number.
 */
ticket::ticket_id_t ticket::get_ticket_number() const
{
    return f_our_ticket;
}


/** \brief Check whether this ticket is locked or not.
 *
 * This function returns true if the ticket is currently locked.
 *
 * \return true when the ticket was successfully locked at some point.
 */
bool ticket::is_locked() const
{
    return f_locked;
}


/** \brief Check whether the system only has one leader.
 *
 * The function check the number of known leaders. If just one, then it
 * returns true. This is important for our algorithm to work properly
 * in that one specific case.
 *
 * \return true if there is only one leader (i.e. one single computer in
 * your whole cluster).
 */
bool ticket::one_leader() const
{
    return f_cluckd->get_computer_count() == 1;
}


/** \brief Get the obtention timeout date.
 *
 * This function returns the obtention timeout. Note that if the lock
 * was already obtained, then this date may be in the past. You can test
 * that by checking the get_lock_timeout() function first.
 *
 * \return The date when the obtention of the ticket timeouts.
 */
cluck::timeout_t ticket::get_obtention_timeout() const
{
    return f_obtention_timeout;
}


/** \brief Define a time when the ticket times out while waiting.
 *
 * This function defines the time threshold when to timeout this
 * ticket in case a service does not reply to an ALIVE message.
 *
 * Whenever a leader dies, a ticket which is not locked yet may be
 * transferred to another leader. To not attempt to lock a ticket
 * for nothing, the new leader first checks that the service
 * which requested that lock is indeed still alive by send an
 * ALIVE message to it. In return, it expects an ABSOLUTELY
 * reply.
 *
 * If the ABSOLUTELY reply does not make it in time (at this time
 * we limit this to 5 seconds) then we consider that this service
 * is not responsive and we cancel the lock altogether.
 *
 * To cancel this timeout, call the function with cluck::timeout_t()
 * in \p timeout (i.e. zero duration).
 *
 * \note
 * Since that message should happen while the cluck daemon
 * is waiting for the LOCK event, the reply should be close to
 * instantaneous. So 5 seconds is plenty until somehow your
 * network is really busy or really large and the time for
 * the message to travel is too long.
 *
 * \param[in] timeout  The time when the ALIVE message times out.
 */
void ticket::set_alive_timeout(cluck::timeout_t timeout)
{
    if(timeout < cluck::timeout_t())
    {
        timeout = cluck::timeout_t();
    }

    if(timeout < f_obtention_timeout)
    {
        f_alive_timeout = timeout;
    }
    else
    {
        // use the obtention timeout if smaller because that was the
        // first premise that the client asked about
        //
        f_alive_timeout = f_obtention_timeout;
    }
}


/** \brief Retrieve the lock duration.
 *
 * This function returns the lock duration in seconds as defined with
 * the constructor.
 *
 * \return The lock duration in seconds.
 */
cluck::timeout_t ticket::get_lock_duration() const
{
    return f_lock_duration;
}


/** \brief Get the lock timeout date.
 *
 * This function returns the lock timeout. If not yet defined, the
 * function will return zero.
 *
 * \note
 * The ticket will immediately be assigned a timeout date when it
 * gets activated.
 *
 * \return The date when the ticket will timeout or zero.
 */
cluck::timeout_t ticket::get_lock_timeout_date() const
{
    return f_lock_timeout_date;
}


/** \brief Get the current lock timeout date.
 *
 * This function returns the "current" lock timeout.
 *
 * The "current" timeout is one of:
 *
 * \li If the lock is being re-requested (after the loss of a leader) then
 *     the ALIVE timeout may be returned for a short period of time.
 *
 * \li If the lock was not yet obtained, this function returns the obtention
 *     timeout timestamp.
 *
 * \li Once the lock was obtained, the lock timeout gets defined and that
 *     one is returned instead.
 *
 * \li When the UNLOCK is received or the timeout happens and cluckd sends
 *     the UNLOCKING message, the function returns the unlock timeout. In
 *     this case, the \em f_lock_time_date field is still used.
 *
 * \note
 * This is the date used in the timed_out() function.
 *
 * \return The date when the ticket will timeout or zero.
 */
cluck::timeout_t ticket::get_current_timeout_date() const
{
    if(f_alive_timeout > cluck::timeout_t())
    {
        return f_alive_timeout;
    }

    if(f_locked)
    {
        return f_lock_timeout_date;
    }

    return f_obtention_timeout;
}


/** \brief Check whether this ticket timed out.
 *
 * This function returns true if the ticket timed out in its current
 * state and should be moved to its next state.
 *
 * The function calls the get_current_timeout_date() to select the correct
 * date. This depends on the current state of the ticket (i.e. maybe we
 * sent the ALIVE message and are using the alive time out value).
 *
 * There are five timeout dates that can happen:
 *
 * 1. Time to obtain a lock
 * 2. Time to keep the lock alive
 * 3. Time to wait for a reply after an UNLOCKING message
 * 4. Time to wait for the UNLOCK message
 * 5. Time to wait for the ALIVE reply (i.e. the ABSOLUTELY message)
 *
 * \return true if the ticket timed out in its current state.
 */
bool ticket::timed_out() const
{
    return get_current_timeout_date() <= snapdev::now();
}


/** \brief Retrieve the object name of this ticket.
 *
 * This function returns the name of the object associated with this
 * lock (i.e. what is being locked).
 *
 * \return The object name of the ticket.
 */
std::string const & ticket::get_object_name() const
{
    return f_object_name;
}


/** \brief Retrieve the tag of this ticket.
 *
 * This function returns the tag of the object associated with this
 * lock (i.e. the specific instance of the lock being locked).
 *
 * \return The tag associated with this ticket.
 */
ed::dispatcher_match::tag_t ticket::get_tag() const
{
    return f_tag;
}


/** \brief Retrieve the server name of this ticket.
 *
 * This function returns the name of the server associated with this
 * lock, i.e. the server to which the LOCKED and UNLOCKED commands are to
 * be sent back to.
 *
 * This name is also used in case of an error to send the LOCKFAILED back
 * to the service that requested the lock.
 *
 * \return The server name of the ticket.
 */
std::string const & ticket::get_server_name() const
{
    return f_server_name;
}


/** \brief Retrieve the service name of this ticket.
 *
 * This function returns the name of the service associated with this
 * lock. This is the service to which the LOCKED and UNLOCKED messages
 * are sent.
 *
 * This name is also used in case of an error to send the LOCKFAILED back
 * to the service that requested the lock.
 *
 * \return The service name of the ticket.
 */
std::string const & ticket::get_service_name() const
{
    return f_service_name;
}


/** \brief Retrieve a reference to the entering key of this ticket.
 *
 * This function returns the entering key of this ticket. The
 * entering key is defined on instantiation so it is always available.
 *
 * \note
 * By contrast, the ticket key is not available up until the time the
 * ticket number is marked as valid.
 *
 * \return The entering key of this ticket.
 */
std::string const & ticket::get_entering_key() const
{
    return f_entering_key;
}


/** \brief Retrieve a reference to the ticket key.
 *
 * This function returns the ticket key of this ticket. The
 * ticket key is only defined at a later time when the ticket has
 * properly entered the bakery. It includes three parameters:
 *
 * \li Ticket number as a hexadecimal number of 8 digits,
 * \li Server name of the server asking for the lock,
 * \li Process Identifier (PID) of the service daemon asking for the lock.
 *
 * \note
 * This function returns an empty string until the ticket key is available.
 *
 * \return The ticket key.
 */
std::string const & ticket::get_ticket_key() const
{
    return f_ticket_key;
}


/** \brief Serialize a ticket to send it over to another leader.
 *
 * This function serialize a ticket to share it with the other 
 * leaders. This is important when a new leader gets elected
 * as it would not otherwise have any idea of what the existing
 * tickets are, although it is not 100% important, if another
 * of the two snaplock was to go down, it becomes primordial
 * for the tickets to be known in the other leaders.
 *
 * This is used at the start before a leader starts accepting new
 * lock requests.
 *
 * \return This ticket as a serialized string.
 *
 * \sa unserialize()
 */
std::string ticket::serialize() const
{
    std::map<std::string, std::string> data;

    data["object_name"]         = f_object_name;
    data["tag"]                 = std::to_string(static_cast<int>(f_tag));
    data["obtention_timeout"]   = f_obtention_timeout.to_timestamp(true);
    //data["alive_timeout"]       = f_alive_timeout.to_timestamp(true); -- we do not want to transfer this one
    data["lock_duration"]       = f_lock_duration.to_timestamp(true);
    data["unlock_duration"]     = f_unlock_duration.to_timestamp(true);
    data["server_name"]         = f_server_name;
    data["service_name"]        = f_service_name;
    data["owner"]               = f_owner;
    if(f_serial != NO_SERIAL)
    {
        data["serial"]          = std::to_string(f_serial);
    }
    data["entering_key"]        = f_entering_key;
    data["get_max_ticket"]      = f_get_max_ticket ? "true" : "false";
    data["our_ticket"]          = std::to_string(f_our_ticket);
    data["added_ticket"]        = f_added_ticket ? "true" : "false";
    data["ticket_key"]          = f_ticket_key;
    data["added_ticket_quorum"] = f_added_ticket_quorum ? "true" : "false";

    // this is a map
    //data["still_entering"]      = f_still_entering;
    //ticket::key_map_t  f_still_entering = key_map_t();

    data["ticket_ready"]        = f_ticket_ready ? "true" : "false";
    data["locked"]              = f_locked ? "true" : "false";
    data["lock_timeout_date"]   = f_lock_timeout_date.to_timestamp(true);

    switch(f_lock_failed)
    {
    case lock_failure_t::LOCK_FAILURE_NONE:
        data["lock_failed"] = "none";
        break;

    case lock_failure_t::LOCK_FAILURE_LOCK:
        data["lock_failed"] = "lock";
        break;

    case lock_failure_t::LOCK_FAILURE_UNLOCKING:
        data["lock_failed"] = "unlocking";
        break;

    }

    std::string result;
    for(auto & it : data)
    {
        result += it.first;
        result += '=';
        // make sure the value does not include any '|'
        result += snapdev::string_replace_many(it.second, {{"|", "%7C"}});
        result += '|';
    }
    result.pop_back();

    return result;
}


/** \brief Unserialize a ticket string back to a ticket object.
 *
 * This function unserialize a string that was generated using the
 * serialize() function.
 *
 * Note that unknown fields are ignored and none of the fields are
 * considered mandatory. Actually the function generates no errors.
 * This means it should be forward compatible.
 *
 * The data gets unserialized in `this` object.
 *
 * \param[in] data  The serialized data.
 */
void ticket::unserialize(std::string const & data)
{
    std::vector<std::string> vars;
    snapdev::NOT_USED(snapdev::tokenize_string(vars, data, "|"));
    for(auto const & d : vars)
    {
        std::string::size_type const pos(d.find('='));
        std::string const name(d.substr(0, pos));
        std::string const value(d.substr(pos + 1));
        switch(name[0])
        {
        case 'a':
            if(name == "added_ticket")
            {
                f_added_ticket = f_added_ticket || value == "true";
            }
            else if(name == "added_ticket_quorum")
            {
                f_added_ticket_quorum = f_added_ticket_quorum || value == "true";
            }
            //else if(name == "alive_timeout") -- we do not transfer this one (not required, and could actually cause problems)
            //{
            //    f_alive_timeout = cluck::timeout_t(value);
            //}
            break;

        case 'e':
            if(name == "entering_key")
            {
#ifdef _DEBUG
                if(f_entering_key != value)
                {
                    // LCOV_EXCL_START
                    throw cluck::logic_error(
                                "ticket::unserialize() not unserializing entering key \""
                                + value
                                + "\" over itself \""
                                + f_entering_key
                                + "\" (entering key mismatch).");
                    // LCOV_EXCL_STOP
                }
#endif
                f_entering_key = value;
            }
            break;

        case 'g':
            if(name == "get_max_ticket")
            {
                f_get_max_ticket = f_get_max_ticket || value == "true";
            }
            break;

        case 'l':
            if(name == "lock_duration")
            {
                f_lock_duration = cluck::timeout_t(value);
            }
            else if(name == "locked")
            {
                f_locked = f_locked || value == "true";
            }
            else if(name == "lock_timeout_date")
            {
                // the time may be larger because of an UNLOCK so we keep
                // the largest value
                //
                cluck::timeout_t const timeout_date(value);
                if(timeout_date > f_lock_timeout_date)
                {
                    f_lock_timeout_date = timeout_date;
                }
            }
            else if(name == "lock_failed")
            {
                // in this case, we avoid reducing the error level
                //
                if(value == "unlocking")
                {
                    f_lock_failed = lock_failure_t::LOCK_FAILURE_UNLOCKING;
                }
                else if(value == "lock" && f_lock_failed == lock_failure_t::LOCK_FAILURE_NONE)
                {
                    f_lock_failed = lock_failure_t::LOCK_FAILURE_LOCK;
                }
            }
            break;

        case 'o':
            if(name == "object_name")
            {
#ifdef _DEBUG
                if(f_object_name != value)
                {
                    // LCOV_EXCL_START
                    throw cluck::logic_error(
                                "ticket::unserialize() not unserializing object name \""
                                + value
                                + "\" over itself \""
                                + f_object_name
                                + "\" (object name mismatch).");
                    // LCOV_EXCL_STOP
                }
#endif
                f_object_name = value;
            }
            else if(name == "obtention_timeout")
            {
                f_obtention_timeout = cluck::timeout_t(value);
            }
            else if(name == "owner")
            {
                f_owner = value;
            }
            else if(name == "our_ticket")
            {
                std::int64_t v;
                advgetopt::validator_integer::convert_string(value, v);
                f_our_ticket = v;
            }
            break;

        case 's':
            if(name == "server_name")
            {
                f_server_name = value;
            }
            else if(name == "service_name")
            {
                f_service_name = value;
            }
            else if(name == "serial")
            {
                std::int64_t v;
                advgetopt::validator_integer::convert_string(value, v);
                f_serial = v;
            }
            break;

        case 't':
            if(name == "tag")
            {
                std::int64_t v;
                advgetopt::validator_integer::convert_string(value, v);
                f_tag = v;
            }
            else if(name == "ticket_key")
            {
                f_ticket_key = value;
            }
            else if(name == "ticket_ready")
            {
                f_ticket_ready = f_ticket_ready || value == "true";
            }
            break;

        case 'u':
            if(name == "unlock_duration")
            {
                f_unlock_duration = cluck::timeout_t(value);
            }
            break;

        }
    }
}



} // namespace cluck_daemon
// vim: ts=4 sw=4 et
