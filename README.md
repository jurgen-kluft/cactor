# xcore actor library (C++)

A tiny actor library focussing mainly on performance which means that:

* Constructing an actor-system you need to supply a number for the maximum number of actors (performance)
* Adding an actor to the system you need to supply a maximum size of the mailbox (performance)
* Actor mailbox is thus fixed-size/bounded and will reject adding messages if full (no-copy, performance)
* Actor can only send messages by pointer (no-copy, performance/simplicity)
* Message will be send back to sender in xactor::returned(xmessage*) (re-use/performance)
* Actor receives messages in xactor::received(xmessage*), actor has to switch:case on the message
  type manually. (simplicity/performance)
* There is only one threading primitive instance in a single actor-system, a semaphore. The mailbox and
  work queue is implemented using lock-free programming.

```c++
    struct mydatamessage : public xmessage
    {
        void    setup(xactor* from, xactor* to, msg_id_t id);
        xbyte   m_data[64];
    };
```

```c++
    class myactor1 : public xactor
    {
        msg_id_t                 m_data_msg_id;
        xfreelist<mydatamessage> m_data_msgs;

        xmailbox*           m_mbox;

    public:
        virtual void setmailbox(xmailbox* mailbox)
        {

        }

        virtual void received(xmessage* msg)
        {
            // Inspect the message and react

            // Send a message back to that actor
            mydatamessage* msg_to_send = m_data_msgs.pop();

            // Fill in the data

            // Send it to the recipient of the incoming message
            m_mbox->send(msg_to_send, msg->get_recipient());
        }

        virtual void  returned(xmessage*& msg)
        {
            if (msg->has_id(m_data_msg_id))
            {
                m_data_msgs.push(msg);
            }

            // Custom code
        }
    };
```

```c++
xsystem*    system = xsystem::boot(2);

xactor*     actor1 = xnew<myactor1>();
xactor*     actor2 = xnew<myactor2>();

system->join(actor1);
system->join(actor2);
```
