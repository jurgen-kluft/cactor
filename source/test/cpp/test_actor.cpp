#include "xbase/x_target.h"
#include "xbase/x_allocator.h"
#include "xactor/x_actor.h"

#include "xunittest/xunittest.h"

using namespace xcore;
extern xcore::alloc_t* gTestAllocator;

class worker_thread_test : public actormodel::worker_thread_t
{
public:
    virtual bool quit() const { return true; }
};

class msg_test : public actormodel::message_t
{
public:
    msg_test(actormodel::actor_t* sender)
    {
        m_id     = actormodel::get_msgid("test");
        m_sender = sender;
    }
};

class work_test : public actormodel::work_t
{
    struct work
    {
        actormodel::actor_t*   m_sender;
        actormodel::message_t* m_message;
        actormodel::actor_t*   m_recipient;
    };

    struct mailbox
    {
        mailbox()
        {
            m_actor = nullptr;
            m_read  = 0;
            m_write = 0;
        }
        actormodel::actor_t* m_actor;
        work                 m_work[32];
        s32                  m_read;
        s32                  m_write;
    };
    mailbox m_mailboxes[32];

    s32 m_queue_count;
    s32 m_queue[32];

public:
    work_test() { m_queue_count = 0; }

    s32 find_mailbox(actormodel::actor_t* actor) const
    {
        for (s32 i = 0; i < 32; i++)
        {
            if (m_mailboxes[i].m_actor == actor)
            {
                return i;
            }
        }
        return -1;
    }

    void register_mailbox(actormodel::actor_t* actor)
    {
        for (s32 i = 0; i < 32; i++)
        {
            if (m_mailboxes[i].m_actor == nullptr)
            {
                m_mailboxes[i].m_actor = actor;
                m_mailboxes[i].m_read  = 0;
                m_mailboxes[i].m_write = 0;
                return;
            }
        }
    }

    bool is_in_queue(actormodel::actor_t* actor) const
    {
        s32 i = find_mailbox(actor);
        if (i >= 0)
        {
            for (s32 j = 0; j < m_queue_count; j++)
            {
                if (m_queue[j] == i)
                {
                    return true;
                }
            }
        }
        return false;
    }

    virtual void add(actormodel::actor_t* sender, actormodel::message_t* msg, actormodel::actor_t* recipient)
    {
        s32 i = find_mailbox(recipient);
        if (i >= 0)
        {
            s32& write                             = m_mailboxes[i].m_write;
            m_mailboxes[i].m_work[write].m_sender  = sender;
            m_mailboxes[i].m_work[write].m_message = msg;
            m_mailboxes[i].m_work[write].m_sender  = recipient;
            if (m_mailboxes[i].m_write == m_mailboxes[i].m_read)
            {
                m_queue[m_queue_count] = i;
                m_queue_count += 1;
            }
            write = (write + 1) & (32 - 1);
        }
    }

    virtual void take(actormodel::worker_t* worker, actormodel::actor_t*& actor, actormodel::message_t*& msg, u32& idx_begin, u32& idx_end)
    {
        if (actor == nullptr)
        {
            if (m_queue_count == 0)
                return;
            m_queue_count -= 1;
            s32 i     = m_queue[m_queue_count];
            actor     = m_mailboxes[i].m_actor;
            idx_begin = m_mailboxes[i].m_read;
            idx_end   = m_mailboxes[i].m_write;
            msg       = m_mailboxes[i].m_work[idx_begin].m_message;
            idx_begin = (idx_begin + 1) & (32 - 1);
        }
        else if (actor != nullptr)
        {
            s32 i     = find_mailbox(actor);
            msg       = m_mailboxes[i].m_work[idx_begin].m_message;
            idx_begin = (idx_begin + 1) & (32 - 1);
        }
    }

    virtual void done(actormodel::worker_t* worker, actormodel::actor_t*& actor, actormodel::message_t*& msg, u32& idx_begin, u32& idx_end)
    {
        if (idx_begin == idx_end)
        {
            s32 i                 = find_mailbox(actor);
            m_mailboxes[i].m_read = idx_end;
            actor                 = nullptr;
            msg                   = nullptr;
            idx_begin             = 0;
            idx_end               = 0;
        }
    }
};

class mailbox_test : public actormodel::mailbox_t
{
public:
    virtual void send(actormodel::message_t* msg, actormodel::actor_t* recipient) {}
};

class actor_test : public actormodel::actor_t
{
    actormodel::mailbox_t* m_mailbox;
    actormodel::id_t       m_msgid;

public:
    actor_test(const char* name) { m_msgid = actormodel::get_msgid(name); }

    virtual void setmailbox(actormodel::mailbox_t* mailbox) { m_mailbox = mailbox; }

    virtual actormodel::mailbox_t* getmailbox() { return m_mailbox; }

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
            actormodel::worker_t* worker = actormodel::create_worker(gTestAllocator);
            worker_thread_test  worker_thread;
            work_test           work;
            actor_test          sender("a");
            actor_test          recipient("b");
            msg_test            msg(&sender);
            work.register_mailbox(&sender);
            work.register_mailbox(&recipient);
            work.add(&sender, &msg, &recipient);

            actormodel::worker_t::ctxt_t worker_context;
            worker->tick(&worker_thread, &work, &worker_context);
            worker->tick(&worker_thread, &work, &worker_context);

            destroy_worker(gTestAllocator, worker);
        }
    }
}
UNITTEST_SUITE_END