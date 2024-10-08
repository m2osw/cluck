
Inter Computer Exclusive Lock
=============================

The `cluck` project is used to obtain locks between computers.

The implementation makes uses of two technologies for very high availability:

1. The [Lamport's bakery algorithm](https://en.wikipedia.org/wiki/Lamport's_bakery_algorithm)
   which allows us to obtain the exclusive lock itself.

2. The QUORUM voting system which allows us to lose any number of computers
   participating to the bakery algorithm and still get locks.

Contrary to most cluster lock, our implementation does not use one leader.
Instead, it uses three which means losing one of the leaders does not
prevent locks from being obtained as expected.


Election Algorithm
==================

Before locks can be provided, the `cluckd` service must reach a valid
state. A valid `cluckd` state means:

* Cluster Up
* Quorum of cluckd services
* Enough cluckd that can be elected
* Valid Election

Until the valid state is reached, `LOCK` requests cannot be answered
positively. What `cluckd` does during that time is register the `LOCK`
requests and if they don't time out before the valid state is reached,
then attempts are made in obtaining the lock.


## Cluster Up

Whenever a `communicatord` daemon connects to another, it adds one to
the total number of connections it has. Once that number is large enough
(reaches a quorum) the cluster is considered to be up.

Note that the cluster up status is enough to ensure a valid election,
however, we decided to also have a quorum of `cluckd` before we move
forward with the election.

The quorum for the cluster up event is calculated using the total number
of `communicatord` in your cluster. Say that number is `n`, the
calculation is:

             n
    quorum = - + 1
             2

In case you have a dynamic cluster, `n` may change over time and this is not
currently supported. In this case, the `cluckd` daemons on those dynamic
instances have to be removed from the list of candidates (i.e. by giving
them the `"off"` priority).


## Quorum of `cluckd`

This means have a number of `cluckd` that are interconnected (have sent
each others a `LOCK_READY` message) equal or larger than the quorum number
of nodes in your cluster. So if you have `n` nodes total, you must have:

             n
    quorum = - + 1
             2

nodes that are interconnected before the election can start.

**Note:** in reality, the number of `cluckd` daemons that are interconnected
does not need to reach that quorum to get a valid election, yet our
implementation still uses that quorum. A `cluckd` election would still be
valid whatever the number of candidates, although at least three candidates
are required (unless your cluster is only 1 or 2 nodes). Yet, using the
`cluckd` daemon quorum is a much easier implementation than only a
**Cluster Up** quorum.


## Valid Election

Our [election process](https://snapwebsites.org/project/cluckd)
is probably better described on our Snap! C++ website.

A valid election ends by entering the _"Leaders Known"_ state.

The basics of the algorithm is a _minima_. Each node is given a unique
identifier. The final three leaders are the three nodes with the
smallest identifier. To reach the concensus, the node with the smallest
IP address is in charge of calculating the results of the election.
There can be only one such node within the `cluckd` quorum.

Just in case, we include an election date parameter. The election with
the largest election date wins.

The identifiers are composed of the following parts:

* priority -- a number from 0 to 15
    * a priority of 0 means that this instance is a leader
    * a priority of 15 means that this computer never participate in elections
      (i.e. `"off"`)
* random number -- a random number (32 bits)
* IP address -- the IP and port of this computer to make the identifier unique
* pid -- the process identifier, to make the identifier more unique
* server name -- the name of the server that owns this node, useful to sort
  this out when the name is not otherwise available and also makes the
  identifier even more unique

Whenever a `cluckd` daemon is started, it calculates its own identifier
once at initialization time.

**Note:** the priority of 15 should be used on nodes that should never become
leaders. This is particularly useful on dynamic nodes if you do not want
leaders to come and go as those nodes are added and removed. Note also
that means your cluster may need a greater connectivity than just a quorum
to handle the case where not enough candidates are available to conduct an
election.


## Messages Involved in the Election Process

The election process mainly works using the `LOCK_READY` and
`LOCK_LEADERS` messages.

It also uses the `STATUS` and `CLUTER_UP` messages as these give the
`cluckd` daemon an idea of which `cluckd` processes are alive and
which are dead.

The `LOCK_READY` message connects all the `cluckd` together.

The `LOCK_LEADERS` is used whenever one of the nodes decided on the
leaders. It broadcasts that information to the rest of the cluster.
As soon as that happens, all the `LOCK_READY` messages will also
include the list of leaders to make sure no additional elections
take place (which is important because the _minima_ is very likely
to change each time an additional computer is added to the cluster).

The basic process is as follow:

     1. `cluckd` starts and gets initialized

     2. `cluckd` connects to `communicatord`

     3. `cluckd` sends a `REGISTER` message

     4. `communicatord` replies with `READY`

     5. `cluckd` checks for the `CLUSTER_STATUS` by sending that message

     6. `cluckd` accepts `STATUS` messages, if too many `cluckd` go down,
        we may drop the leaders

     7. `cluckd` accepts `CLUSTER_DOWN` messages, if received, kill the
        current status since it cannot be valid anymore, this instance
        cannot create locks anymore

     8. `cluckd` receives a `CLUSTER_UP` message, check the election status
        and start sending `LOCK_READY` messages

     9. whenever `cluckd` receives a `LOCK_READY`, record the information
        if it was not yet available and then check the election status

    10. if the election status changes to valid in a `LOCK_READY`, then
        elect the leaders and send a `LOCK_LEADERS`

    11. when receiving a `LOCK_LEADERS`, register the new leaders

This is a rather simplified list of steps that are used to handle the
`cluckd` elections. Steps 1 through 4 are initialization and should
happen only once. The other steps are asynchronous and happen as
new messages are received.


Lamport's bakery algorithm for the Lock
=======================================

The [Lamport's bakery algorithm](https://en.wikipedia.org/wiki/Lamport's_bakery_algorithm)
is shown on Wikipedia. However, we have two major differences:

1. We use the algorithm to lock between any number of computers, not just
one computer with multiple threads. Our algorithm is used to lock processes
and make sure that we do not spinlock or poll at any point (i.e. we make
a request for a lock, then wait by falling asleep, then get awaken either
because the lock was obtained or because the lock could not be obtained
in time--or in some cases because something else failed.)

2. The algorithm is programmed to be elastic. This means the number of
computers that can be locked can dynamically grow and shrink. (i.e. The
default algorithm has an assigned number of processes it can lock and
unlock.)

The one important part for the algorithm to function properly is to make
sure that each computer has a different name. The index (`j` in the
Wikipedia example) must be unique and we use the computer name for that
purpose.

In the Snap! environment, the `communicatord` daemon is responsible
for connecting remote computers together. If a `communicatord` receives
two requests from two different computers to `CONNECT` and both have the
same name, the second connection is always refused.


The QUORUM Agreement
====================

Whenever someone wants to obtain a lock, various steps have to be taken
as shown in the Lamport's bakery algorithm. Those steps change the value
of a Boolean (`Entering`) or an integer (`Number`).

Since our implementation has one constraint: continue to function without
interruption even when a computer goes down, we use a voting system
between the `cluckd` daemons that are currently running.

This works in a way which is very similar to the Cassandra consistency
level named QUORUM. We expect at least `N / 2 + 1` computers to accept
a message before we can go on. By accepting the message, a computer
writes the value we sent to it and replies to the message with an
acknowledgement. Once we get at least `N / 2 + 1` acknowledgement replies,
we can move on to the next step.

For example, the `ENTERING` message is sent by the `cluckd` daemon
requiring a lock. This marks the specified computer and lock (each lock
is given a "name" which most often is a URI) as `ENTERING`. So `cluckd`
broadcast the message to all the other `cluckd` it knows about and waits
for them to reply with `ENTERED`. When they do so, they also save an
object saying that this specific computer and lock are being entered.
Once the first `cluckd` received enough `ENTERED` messages, it views
that one lock request as entered and it can start the next process:
retrieve the largest ticket number. This works in a similar fashion,
by sending another event and waiting for its acknowledgement.

Note that the current implementation (As of Oct 4, 2016) is not 100%
safe because it does not record who answered each messages. We cannot
hope that if A acknowledges message 1 and then B acknowledge message 2,
then everything is good. We need A to continue to acknowledge our
messages and we must ignore B if it never acknowledge message 1. This
will be fixed in a later version.


Semaphore Support
=================

At some point we want to add support for semaphores. This will allow
us to cleanly drain all requests to a website by creating a semaphore
that can either be locked in `READ-ONLY` mode (normal client access) or
in `READ/WRITE` mode.

Each client accessing a website through the `snap_child` class will
have to obtain a `READ-ONLY` semaphore. If that fails, then someone
else is running a process in `READ/WRITE` mode and we have to refuse
the session altogether (i.e. switch to showing a static page saying
something like "site is under maintenance").

Backend processes that change the database in ways that are not
compatible to inter-process processing, namely, the `snapinstallwebsite`
process, will request a `READ/WRITE` access to the semaphore. This
will prevent any further `READ-ONLY` requests from being accepted
until the `READ/WRITE` process is done.

We want to look into offering several ways to enter the `READ/WRITE`
mode in order to make it nicer:

1. hard locking--in this mode a `READ/WRITE` request is given a spot
   in the list of incoming locks. It blocks any further `READ-ONLY`
   locks so it gets its turn as fast as possible (as soon as all the
   existing `READ-ONLY` are done). This is the default.

       cluck::type_t::CLUCK_TYPE_READ_WRITE_PRIORITY

2. soft locking--in this mode, the `READ/WRITE` lock does not prevent
   further clients from obtaining the `READ-ONLY` lock. This means the
   `READ/WRITE` may not happen for some time (it does not lock its position
   in the lock FIFO).

       cluck::type_t::CLUCK_TYPE_READ_WRITE

3. timed soft locking--like the _soft locking_ but further waits for a
   little while after the lock is obtainable before trying to obtain the
   lock. The idea behind this one is to allow a client to finish browsing,
   because once we get the `READ/WRITE` lock, the client won't be able to
   access the site for a while. The wait is calculated from the last
   `READ-ONLY` lock to the new `READ/WRITE` lock. It could be as long
   as 10 minutes. If no other `READ-ONLY` happened within 10 minutes,
   then go ahead and try to obtain the `READ/WRITE` lock and do your work.

       cluck::type_t::CLUCK_TYPE_READ_WRITE_GENTLE

Any of these locks will have timeouts. If the timeout is passed before
the lock can be obtained, the lock fails.

The semaphore locks will be used to drain users from a website so
we can run `snapinstallwebsite` (i.e. upgrade) without end user
interferences.

Note that this is different from draining a computer from client
accesses.


Optimizations
=============

The daemon uses an optimization flag that can be used to avoid many strings
being generated, especially on failures.

By default, this flag is turned off. You can turn it on using the following:

    cmake -DCLUCKD_OPTIMIZATIONS:BOOL=on ...


Bugs
====

Submit bug reports and patches on
[github](https://github.com/m2osw/cluck/issues).

# vim: ts=2 sw=2 et

_This file is part of the [snapcpp project](https://snapwebsites.org/)._
