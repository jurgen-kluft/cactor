#include "cbase/c_target.h"
#include "cbase/c_allocator.h"
#include "cactor/c_actor.h"

#include "cunittest/cunittest.h"

using namespace ncore;

class msg_test : public actormodel::message_t
{
public:
    msg_test(actormodel::actor_t* sender)
    {
        m_id     = actormodel::get_msgid("test");
        m_sender = sender;
    }
};

class actor_test : public actormodel::handler_t
{
    actormodel::id_t       m_msgid;

public:
    actor_test(const char* name) { m_msgid = actormodel::get_msgid(name); }

    virtual void received(actormodel::message_t* msg)
    {
        // do something with the message
    }

    virtual void returned(actormodel::message_t*& msg)
    {
        // garbage collect the message
    }
};

UNITTEST_SUITE_BEGIN(xactor_unittest)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(test1)
        {

        }
    }
}
UNITTEST_SUITE_END