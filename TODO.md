
* The server is called cluckd, the library is just cluck. The debian package
  is also just cluck. The logger is told to log in cluckd.log and the name
  of the service given to communicatord is cluckd. I'm wondering whether we
  should have two packages: cluck & cluckd. In particular, with just cluck
  we would be able to create a lock and messages would automatically be sent
  to a cluckd running on another computer.

* The `TRANSMISSION_REPORT` message from the communicator does not include
  any serial number or tag to recognize the cluck that sent the message.
  (i.e. this is not a problem if an app. requests only one lock at a time,
  it can be a huge issue with multiple locks and it happens...) One solution
  is to include a serial parameter and have the transmission report always
  include that parameter in the reply. The other way is to cancel all the
  locks on such failures (which is how it's done at the moment).

* Implement support for semaphores (i.e. "read-only" lock that multiple
  instances can obtain in parallel).

* Implement a "lock status" message one can listen to in order to know
  things such as how much longer it will take for a lock to succeed
  giving applications a way to decide whether to continue to wait or not.

* Look into properly handling the case where the other two leaders go out
  and we are currently waiting on replies that will never make it. For example,
  the ticket::entered() sends a `GET_MAX_TICKET` event. If we never receive
  a reply that lock will linger forever (i.e. no timeout exists for that
  and most of the other inter-leader messages).

* Convert the Paxos conversation to use UDP, albeit probably not in
  broadcast mode because VPNs do not always allow it and for only
  3 instances (i.e. leaders) of cluckd, it would be a huge waste if
  broadcasting to 100 computers in the cluster.

* When we lose the cluster (see `CLUSTER_DOWN` message handling), we void
  the existing locks, but I do think that this is not required.

