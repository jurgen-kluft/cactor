#include "ccore/c_target.h"
#include "ccore/c_allocator.h"

#include "cactor/private/c_msg_pool.h"
#include "cactor/private/c_message.h"

#include "cunittest/cunittest.h"

using namespace ncore;

struct myactor
{
    nactor::msg_pool_t* m_pool;
    nactor::msg_t       m_messages[10]; // Preallocated message storage

    myactor()
        : m_pool(nullptr)
    {
    }

    void init_msg_pool()
    {
        // Populate the message pool with preallocated messages
        m_pool = nactor::msg_pool_create();
        msg_pool_populate(m_pool, m_messages, 10);
    }
};

// Explicit definition of your garbage collection system mapping to the pool
void actor_gc(void* user, nactor::msg_t* msg)
{
    myactor* actor = (myactor*)user;

    // Clear out receiving details for safety
    msg->m_sender    = nullptr;
    msg->m_recipient = nullptr;

    // Lock-free push the message directly back into the originating pool
    msg_pool_push(actor->m_pool, msg);
}

UNITTEST_SUITE_BEGIN(msg_pool)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(test1)
        {
            // Create an actor that will use this pool for garbage collection
            myactor actor;
            actor.init_msg_pool(); // Populate the pool with preallocated messages

            // Allocate a message from the pool
            nactor::msg_t* msg = nactor::msg_pool_pop(actor.m_pool);
            ASSERT(msg != nullptr); // Ensure allocation was successful

            // Simulate sending the message to the actor (which will trigger GC)
            actor_gc(&actor, msg);

            // After GC, the message should be back in the pool and available for allocation
            nactor::msg_t* msg2 = nactor::msg_pool_pop(actor.m_pool);
            ASSERT(msg2 == msg); // The same message should be allocated again

            // Clean up the pool after testing
            nactor::msg_pool_destroy(actor.m_pool);
        }
    }
}
UNITTEST_SUITE_END
