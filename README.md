# xcore actor library (C++)

A tiny actor library focussing mainly on performance which means that:

* Constructing an actor-system you need to supply a number for the maximum number of actors (performance)
* Adding an actor to the system you need to supply a maximum size of the mailbox (performance)
* Actor mailbox is thus fixed-size/bounded and will reject adding messages if full (no-copy, performance)
* Actor can only send messages by pointer (no-copy, performance/simplicity)
* Message will be send back to sender in xactor::returned(xmessage*) (re-use/performance)
* Actor receives messages in xactor::received(xmessage*), actor has to switch:case on the message
  type manually. (simplicity/performance)
* There is only one threading primitive instance in a single actor-system, a semaphore. mailbox and
  work queue is implemented using lock-free programming.

