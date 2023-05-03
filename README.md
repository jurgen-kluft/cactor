# ccore actor library (C++)

A tiny actor library focussing mainly on performance which means that:

* Constructing an actor-system you need to supply a number for the maximum number of actors (performance)
* Adding an actor to the system you need to supply a maximum size of the mailbox (performance)
* Actor mailbox is thus fixed-size/bounded and will reject adding messages if full (no-copy, performance)
* Actor can only send messages by pointer (no-copy, performance/simplicity)
* Message will be send back to sender in actor_t::returned(message_t*) (re-use/performance)
* Actor receives messages in actor_t::received(message_t*), actor has to switch:case on the message
  type manually. (simplicity/performance)

```c++
    struct mydatamessage : public message_t
    {
        void    setup(actor_t* from, actor_t* to, msg_id_t id);
        xbyte   m_data[64];
    };
```

```c++
    class myactor1 : public actor_t
    {
        msg_id_t                  m_data_msg_id;
        freelist_t<mydatamessage> m_data_msgs;
        mailbox_t*                m_mbox;

    public:
        virtual void setmailbox(mailbox_t* mailbox)
        {
            m_mbox = mailbox;
        }

        virtual void received(message_t* msg)
        {
            // Inspect the message and react

            // Send a message back to that actor
            mydatamessage* msg_to_send = m_data_msgs.pop();

            // Fill in the data

            // Send it to the recipient of the incoming message
            m_mbox->send(msg_to_send, msg->get_recipient());
        }

        virtual void returned(message_t*& msg)
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
system_t*    system = nsystem::boot(2);

actor_t*     actor1 = g_New<myactor1>();
actor_t*     actor2 = g_New<myactor2>();

system->join(actor1);
system->join(actor2);
```
