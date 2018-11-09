#include "xbase/x_allocator.h"
#include "xactor/x_actor.h"

#ifdef TARGET_PC
	#include <windows.h>
	#include <stdio.h>
	#include <atomic>
#endif


namespace xcore
{
	class s32atomic
	{
		std::atomic<s32> m_value;
	public:
		s32				load() { return m_value.load(); }
		bool			cas(s32 old, s32 _new) { return m_value.compare_exchange_weak(old, _new); }
	};

	// We base the receiving of messages on simple structs, messages are
	// always send back to the sender for garbage collection to simplify
	// creation, re-use and destruction of messages.

	// There are a fixed number of worker-threads, initialized according to what
	// the user needs. The user can use the xsystem package to identify how
	// many physical and logical cores this machine has as well as how many
	// hardware threads.

	// C 1 | R 15 | W 15

	const u32 WRITE_INDEX_SHIFT = 0;
	const u32 WRITE_INDEX_BITS = 12;
	const u32 WRITE_INDEX_MASK = ((u32(1) << WRITE_INDEX_BITS) - 1) << WRITE_INDEX_SHIFT;
	const u32 READ_INDEX_SHIFT = 16;
	const u32 READ_INDEX_BITS = 12;
	const u32 READ_INDEX_MASK = ((u32(1) << READ_INDEX_BITS) - 1) << READ_INDEX_SHIFT;
	const u32 QUEUED_FLAG_SHIFT = 31;
	const u32 QUEUED_FLAG = u32(1) << QUEUED_FLAG_SHIFT;
	const u32 MAX_MESSAGES = (u32(1) << WRITE_INDEX_BITS);

	// Queue using a lock-free ringbuffer, it is important that this queue
	// is initialized with a size that never will be reached. This queue
	// will fail HARD when messages are queued up and the size is exceeding
	// the initialized size.
	// The initialized size of this queue cannot exceed 4096 messages.
	// When writing a system using this actor-model messages should be
	// pre-allocated (bounded) and not dynamically allocated.
	class xqueue
	{
		u32					m_size;
		void**				m_queue;
		s32atomic*			m_index;

	public:
		void				init(x_iallocator* allocator, s32 max_messages)
		{
			m_queue = (void**)allocator->allocate(max_messages * sizeof(void*), sizeof(void*));
			m_index = (s32atomic*)allocator->allocate(32, 32);
			m_size = max_messages;
		}

		void				destroy(x_iallocator* allocator)
		{
			allocator->deallocate(m_index);
			allocator->deallocate(m_queue);
		}

		s32					push(void* p)
		{
			// We are pushing the message when the actor is QUEUED or NOT-QUEUED.
			s32 i, n;
			do {
				i = m_index->load();
				n = ((i + 1) & WRITE_INDEX_MASK) | QUEUED_FLAG;
			} while (!m_index->cas(i,n));

			m_queue[i & (m_size-1)] = p;

			// Did we push a message and the actor is not queued ?
			// If so we need to signal the caller that it should
			// queue the actor. We already set the QUEUED_FLAG.
			s32 const queue = ((i & QUEUED_FLAG) == (n & QUEUED_FLAG)) ? 0 : 1;
			return queue;
		}


		void				claim(u32& idx, u32& end)
		{
			s32 i = m_index->load();
			idx = u32((i & READ_INDEX_MASK) >> READ_INDEX_SHIFT);
			end = u32((i & WRITE_INDEX_MASK) >> WRITE_INDEX_SHIFT);
		}

		void				deque(u32& idx, u32 end, void*& p)
		{
			p = m_queue[idx & (m_size-1)];
			idx += 1;
		}

		s32					release(u32 idx, u32 end)
		{
			// This is our new 'read' index
			bool const do_mask = (end >= m_size);
			u32 const read = do_mask ? end & (m_size - 1) : end;

			s32 i,n;
			do {
				i = m_index->load();
				u32 write = u32((i & WRITE_INDEX_MASK) >> WRITE_INDEX_SHIFT);
				if (do_mask)
				{
					write = write && (m_size - 1);
				}
				n = ((read & READ_INDEX_MASK) << READ_INDEX_SHIFT) | (write << WRITE_INDEX_SHIFT);
				if (read < write)
				{	// Let ourselves be queued since we have more messages to process
					n |= QUEUED_FLAG;
				}
			} while (!m_index->cas(i, n));

			// So we now have updated 'm_index' with 'read'/'write' and have succesfully updated the QUEUED_FLAG (set or unset)
			return (n & QUEUED_FLAG) == 0 ? 0 : 1;
		}
	};

	class xmessageslf : public xmessages
	{
		xqueue		m_queue;
	
	public:
		void				init(x_iallocator* allocator, s32 max_messages)
		{
		}

		virtual s32			push(xmessage* msg)
		{
			void * p = (void *)msg;
			return m_queue.push(p);
		}

		virtual void		claim(u32& idx, u32& end)
		{
			m_queue.claim(idx, end);
		}

		virtual void		deque(u32& idx, u32 end, xmessage*& msg)
		{
			void* p;
			m_queue.deque(idx, end, p);
			msg = (xmessage*)p;
		}

		virtual s32			release(u32 idx, u32 end)
		{
			return m_queue.release(idx, end);
		}
	};

	class xactor_mailbox : public xmailbox
	{
	public:
		void				initialize(xactor* actor, xwork* work, x_iallocator* allocator, s32 max_messages)
		{
			m_actor = actor;
			m_work = work;

			x_type_allocator<xmessageslf> xmessages_type(allocator);
			m_messages = xmessages_type.allocate();

		}

		xactor *			m_actor;
		xwork *				m_work;
		virtual void		send(xmessage* msg, xactor* recipient)
		{
			m_work->add(m_actor, msg, recipient);
		}

		s32					push(xmessage* msg);
		void				claim(u32& idx, u32& end);					// return [i,end] range of messages
		void				deque(u32& idx, u32 end, xmessage*& msg);
		s32					release(u32 idx, u32 end);				// return 1 when there are messages pending

		xmessages *			m_messages;
	};

	s32		xactor_mailbox::push(xmessage* msg)
	{
		if (m_messages->push(msg) > 0) {
			return 1;
		}
		return 0;
	}

	void	xactor_mailbox::claim(u32& idx, u32& end)
	{
		m_messages->claim(idx, end);
	}

	void	xactor_mailbox::deque(u32& idx, u32 end, xmessage*& msg)
	{
		m_messages->deque(idx, end, msg);
	}

	s32		xactor_mailbox::release(u32 idx, u32 end)
	{
		if (m_messages->release(idx, end) > 0) {
			return 1;
		}
		return 0;
	}

	class xsemaphore_imp : public xsemaphore
	{
		HANDLE				ghSemaphore;

	public:
		virtual void		setup(s32 initial, s32 maximum)
		{
			ghSemaphore = ::CreateSemaphore(
				NULL,           // default security attributes
				initial,		// initial count
				maximum,		// maximum count
				NULL);          // unnamed semaphore
		}

		virtual void		teardown()
		{
			CloseHandle(ghSemaphore);
		}

		virtual void		request()
		{
			DWORD dwWaitResult = WaitForSingleObject(ghSemaphore, INFINITE);
			switch (dwWaitResult)
			{
			case WAIT_OBJECT_0:	// The semaphore object was signaled.

				break;
			case WAIT_FAILED:
				break;
			}
		}

		virtual void		release()
		{
			::ReleaseSemaphore( ghSemaphore/*handle to semaphore*/, 1 /*increase count by one*/, NULL);
		}

		XCORE_CLASS_PLACEMENT_NEW_DELETE
	};

	class xwork_queue
	{
		xqueue				m_queue;
		xsemaphore*			m_sema;

	public:
		void				initialize(x_iallocator* allocator, s32 max_actors)
		{
			void** queue = (void**)allocator->allocate(sizeof(void*) * max_actors, sizeof(void*));
			m_queue.init(queue, max_actors);

			x_type_allocator<xsemaphore_imp> sema_type(allocator);
			m_sema = sema_type.allocate();
		}

		void				push(xactor* actor)
		{
			void * p = (void *)actor;
			m_queue.push(p);
			m_sema->release();
		}

		void				pop(xactor*& actor)
		{
			void * p;
			m_sema->request();
			m_queue.pop(p);
			actor = (xactor*)p;
		}
	};

	class xwork_imp : public xwork
	{
		xwork_queue			m_queue;

	public:
		void				init(x_iallocator* allocator, s32 max_actors)
		{
			n_queue.initialize(allocator, max_actors);
		}

		// @Note: This can be called from multiple threads!
		void				add(xactor* sender, xmessage* msg, xactor* recipient)
		{
			xactor_mailbox* mb = static_cast<xactor_mailbox*>(recipient->getmailbox());
			if (mb->push(msg) == 1) 
			{
				// mailbox indicated that we have pushed a message and the actor is already
				// marked as 'idle' and we are the one here to push him in the work-queue.
				m_queue.push(recipient);
			}
		}

		void				take(xworker_thread* thread, xactor*& actor, xmessage*& msg, u32& msgidx, u32& msgend)
		{
			if (actor == NULL)
			{
				// This will make the calling thread block if the queue is empty
				m_queue.pop(actor);

				xactor_mailbox* mb = static_cast<xactor_mailbox*>(actor->getmailbox());
				// Claim a batch of messages to be processed here by this worker
				mb->claim(msgidx, msgend);
				mb->deque(msgidx, msgend, msg);
			}
			else
			{
				// Take the next message out of the batch from the mailbox of the actor
				xactor_mailbox* mb = static_cast<xactor_mailbox*>(actor->getmailbox());
				mb->deque(msgidx, msgend, msg);
			}
		}

		void				done(xworker_thread* thread, xactor*& actor, xmessage*& msg, u32& msgidx, u32& msgend)
		{
			// If 'msgidx == msgend' then try and add the actor back to the work-queue since
			// it was the last message we supposed to have processed from the batch.
			if (msgidx == msgend)
			{
				xactor_mailbox* mb = static_cast<xactor_mailbox*>(actor->getmailbox());
				if (mb->release(msgidx, msgend) == 1)
				{
					// mailbox indicated that we have to push back the actor in the work-queue
					// because there are new messages pending.
					m_queue.push(actor);
				}
				actor = NULL;
			}
			msg = NULL;
		}
	};

	class xworker_imp: public xworker
	{
	public:
		void				run(xworker_thread* thread, xwork* work)
		{
			u32 i, e;
			xactor* actor;
			xmessage* msg;

			while (thread->quit()==false)
			{
				// Try and take an [actor, message] piece of work
				work->take(thread, actor, msg, i, e);
				
				// Let the actor handle the message
				if (msg->is_recipient(actor))
				{
					actor->process(msg);
					if (msg->is_sender(actor))
					{
						// Garbage collect the message immediately
						actor->gc(msg);
					}
					else 
					{
						// Send this message back to sender
						work->add(msg->get_recipient(), msg, msg->get_sender());
					}
				}
				else if (msg->is_sender(actor))
				{
					actor->gc(msg);
				}

				// Report the [actor, message] back as 'done'
				work->done(thread, actor, msg, i, e);
			}
		}
	};

}
