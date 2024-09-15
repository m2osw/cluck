
* The server is called cluckd, the library is just cluck. The debian package
  is also just cluck. The logger is told to log in cluckd.log and the name
  of the service given to communicatord is cluckd. I'm wondering whether we
  should have two packages: cluck & cluckd. In particular, with just cluck
  we would be able to create a lock and messages would automatically be sent
  to a cluckd running on another computer. i.e. we'd eliminate the need to
  run a cluckd on every single machine.

  **IMPORTANT NOTE:** The current implementation uses the `CLUSTER_UP`
                      number of neighbors to decide what the quorum and
		      some other numbers are. Making it possible to not
		      have the cluckd service on all computers complicates
		      that part of the existing implementation.

* The election algorithm is not 100% safe unless we wait for `CLUSTER_COMPLETE`.
  With just a `CLUSTER_UP` plus a quorum, we could end up with two lists of
  cluck daemons that look as follow:

      A B   B   B B B   B B B

          A   B B B   B B B   B B

  Here the result is that two different computers (marked `A`) think that
  they have the lowest IP address and they send a `LOCK_LEADERS` message
  as a result.

  We need to make sure that will work as expected. The broadcasting means
  that the communicatord system will send the message to all cluckd even
  though both `A` computers do not yet know about each other.

* The `TRANSMISSION_REPORT` message from the communicator does not include
  any serial number or tag to recognize the cluck that sent the message.
  (i.e. this is not a problem if an app. requests only one lock at a time,
  it can be a huge issue with multiple locks and it happens...) One solution
  is to include a serial parameter and have the transmission report always
  include that parameter in the reply. The other way is to cancel all the
  locks on such failures (which is how it's done at the moment).

* Implement support for semaphores (i.e. "read-only" lock that multiple
  instances can obtain simultaneously).

* Implement a "lock status" message one can listen to in order to know
  things such as how much longer it will take for a lock to succeed
  giving applications a way to decide whether to continue to wait or not.

* Convert the Paxos conversation to use UDP, albeit probably not in
  broadcast mode because VPNs do not always allow it and for only
  3 instances (i.e. leaders) of cluckd, it would be a huge waste if
  broadcasting to 100 computers in the cluster.

* When we lose the cluster (see `CLUSTER_DOWN` message handling), we void
  the existing locks, but I do think that this is not required. i.e. new
  locks cannot safely be obtained, but old existing locks when our leaders
  did not go down are still safe (unless they came from one of the computers
  that just went down).

