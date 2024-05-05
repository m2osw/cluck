
* The `TRANSMISSION_REPORT` message from the communicator does not include
  any serial number or tag to recognize the cluck that sent the message.
  (i.e. this is not a problem if an app. require only one lock at a time,
  it can be a huge issue with multiple locks and it happens...) One solution
  is to include a serial parameter and have the transmission report always
  include that parameter in the reply. The other way is to cancel all the
  locks on such failures (which is how it's done at the moment).

* Implement support for semaphores (i.e. "read-only" lock that multiple
  instances can obtain in parallel)

* Implement a "lock status" message one can listen to in order to know things
  such as how much longer it will take for a lock to succeed giving
  applications a way to decide whether to continue to wait or not

* Convert the Paxos conversation to use UDP, albeit probably not in broadcast
  mode because VPN do not always allow it and if we only have 3 to 5 instances
  of cluckd when we use 100 computers, it would be a huge waste

* When we lose the cluster (see `CLUSTER_DOWN` message handling), we void the
  existing locks, but I do think that this is not required

