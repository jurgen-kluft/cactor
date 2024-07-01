# ccore actor library (C++)

A tiny actor library focussing mainly on performance which means that:

* Constructing an actor-system you need to supply a number for the maximum number of actors
* Need to provide beforehand the maximum number of messages that can be queued for actors
* Actor can only send messages by pointer (no value copy, performance/simplicity)
* Message will be send back to sender in `actor_t::returned(message_t*)` (re-use/performance/garbage-collection)
* Actors receive messages in `actor_t::received(message_t*)`, actor has to switch:case on the message
  type manually. (simplicity/performance)

```c++
    struct mydatamessage : public message_t
    {
        void    setup(actor_t* from, actor_t* to, msg_id_t id);
        byte    m_data[64];
    };
```

```c++
    class actor : public handler_t
    {
        actor_t*                  m_actor;
        system_t*                 m_system;
        msg_id_t                  m_data_msg_id;
        freelist_t<mydatamessage> m_data_msgs;

    public:
        void join(system_t* system)
        {
            m_system = system;
            m_actor = actor_join(system, this);
        }

        virtual void received(message_t* msg)
        {
            // Inspect the message and react

            // Send a message back to that actor
            mydatamessage* msg_to_send = m_data_msgs.pop();

            // Fill in data

            // Send it to the recipient of the incoming message
            actor_send(m_system, m_actor, msg_to_send, msg->get_recipient());
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
system_t*    system = nactor::create_system(allocator, 8, 10, 1024, 32);

// user needs to have classes implemented that derived from nactor::handler_t

actor_t*     actor1 = actor_join(system, handler1);
actor_t*     actor2 = actor_join(system, handler2);

// if the user wants to send message from the main thread and other threads, then
// on each thread he needs to reserve a 'producer' index.
```
