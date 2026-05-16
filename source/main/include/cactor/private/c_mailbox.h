#ifndef __CACTOR_MAILBOX_H__
#define __CACTOR_MAILBOX_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nactor
    {
        struct msg_t;

        struct mailbox_t;
        mailbox_t* mailbox_create();
        void       mailbox_destroy(mailbox_t* mbox);

        bool   mailbox_push(mailbox_t* mbox, msg_t* msg);
        msg_t* mailbox_rotate(mailbox_t* mbox);
        bool   mailbox_finalize(mailbox_t* mbox);

    } // namespace nactor
} // namespace ncore

#endif // __CACTOR_MAILBOX_H__
