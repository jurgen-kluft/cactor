#include "xbase/x_target.h"
#include "xbase/x_allocator.h"
#include "xactor/x_actor.h"

#include "xunittest/xunittest.h"

using namespace xcore;
extern xcore::alloc_t* gTestAllocator;

class xworker_thread_test : public xcore::xworker_thread
{
public:
    virtual bool quit() const { return true; }
};

class xmsg_test : public xmessage
{
public:
    xmsg_test(xactor* sender, xactor* recipient)
    {
        m_id = xmsgid("xmsg_test");
        m_sender = sender;
        m_message = this;
        m_recipient = recipient;
    }
};

class xwork_test : public xcore::xwork
{
    struct work
    {
        xactor*   m_sender;
        xmessage* m_message;
        xactor*   m_recipient;
    };

    struct mailbox
    {
        mailbox()
        {
            m_actor = nullptr;
            m_read  = 0;
            m_write = 0;
        }
        xactor* m_actor;
        work    m_work[32];
        s32     m_read;
        s32     m_write;
    };
    mailbox m_mailboxes[32];

    s32 m_queue_count;
    s32 m_queue[32];

public:
    xwork_test() {}

    s32 find_mailbox(xactor* actor)
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

    bool is_in_queue(xactor* actor) const
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

    virtual void add(xactor* sender, xmessage* msg, xactor* recipient)
    {
        s32 i = find_mailbox(recipient);
        if (i >= 0)
        {
            s32& write                             = m_mailboxes[i].m_write;
            m_mailboxes[i].m_work[write].m_sender  = sender;
            m_mailboxes[i].m_work[write].m_message = message;
            m_mailboxes[i].m_work[write].m_sender  = recipient;
            write                                  = (write + 1) & (32 - 1);
        }
    }

    virtual void queue(xactor* actor)
    {
        s32 i = find_mailbox(recipient);
        if (i >= 0)
        {
            m_queue[m_queue_count] = i;
            m_queue_count += 1;
        }
    }

    virtual void take(xworker* worker, xactor*& actor, xmessage*& msg, u32& idx_begin, u32& idx_end)
    {
        if (actor == nullptr)
        {
            if (m_queue_count == 0)
                return;
            m_queue_count -= 1;
            actor     = m_queue[m_queue_count];
            s32 i     = find_mailbox(actor);
            idx_begin = m_mailboxes[i].m_read;
            idx_end   = m_mailboxes[i].m_write;
            s32 i     = find_mailbox(actor);
            msg       = m_mailboxes[i].m_work[idx_begin];
            idx_begin = (idx_begin + 1) & (32 - 1);
        }
        else if (actor != nullptr)
        {
            s32 i     = find_mailbox(actor);
            msg       = m_mailboxes[i].m_work[idx_begin];
            idx_begin = (idx_begin + 1) & (32 - 1);
        }
    }

    virtual void done(xworker* worker, xactor*& actor, xmessage*& msg, u32& idx_begin, u32& idx_end)
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

class xmailbox_test : public xmailbox
{
public:
    virtual void send(xmessage* msg, xactor* recipient) {}
};

class xactor_test : public xactor
{
    xmailbox* m_mailbox;

public:
    virtual void setmailbox(xmailbox* mailbox) { m_mailbox = mailbox; }

    virtual xmailbox* getmailbox() { return m_mailbox; }

    virtual void received(xmessage* msg)
    {
        // do something with the message
    }

    virtual void returned(xmessage*& msg)
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
            xworker_thread_test worker_thread;
            xworker*            worker = create_worker(gTestAllocator);
            xwork_test          work;
            xactor_test         sender;
            xactor_test         recipient;
            xmsg_test           msg(sender, recipient);
            work.
            xworker::ctxt_t     worker_context;
            worker->tick(&worker_thread, &work, &worker_context);
        }
    }
}
UNITTEST_SUITE_END