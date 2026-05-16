# ccore actor library (C++)

A tiny actor library focussing mainly on performance and simplicity.

## usage

```c++
    struct mydatamessage : public nactor::message_t
    {
        void    setup(nactor::actor_t* from, nactor::actor_t* to, msg_id_t id);
        byte    m_data[64];
    };

    class myactor : public nactor::handler_t
    {
        nactor::actor_t*                  m_actor;
        nactor::system_t*                 m_system;
        nactor::msg_id_t                  m_data_msg_id;
        nactor::freelist_t<mydatamessage> m_data_msgs;

    public:
        void join(nactor::system_t* system)
        {
            m_system = system;
            m_actor = nactor::actor_join(system, this);
        }

        virtual void received(nactor::message_t* msg)
        {
            // Inspect the message and react

            // Send a message back to that actor
            mydatamessage* msg_to_send = m_data_msgs.pop();

            // Fill in data

            // Send it to the recipient of the incoming message
            nactor::actor_send(m_system, m_actor, msg_to_send, msg->get_recipient());
        }

        virtual void returned(nactor::message_t*& msg)
        {
            if (msg->has_id(m_data_msg_id))
            {
                m_data_msgs.push(msg);
            }

            // Custom code
        }
    };

void some_function()
{
    nactor::allocator_t* allocator = nactor::allocator_t::get_system();
    nactor::system_t*    system = nactor::create_system(allocator, 8, 10, 1024, 32);

    // user needs to have classes implemented that derived from nactor::handler_t

    nactor::actor_t*     actor1 = nactor::actor_join(system, handler1);
    nactor::actor_t*     actor2 = nactor::actor_join(system, handler2);

    // if the user wants to send message from the main thread and other threads, then
    // on each thread he needs to reserve a 'producer' index.
}

```
