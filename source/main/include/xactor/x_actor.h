#ifndef __X_ACTOR_ACTOR_H__
#define __X_ACTOR_ACTOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE 
#pragma once 
#endif


namespace xcore
{
	typedef		u64		msg_id_t;
	typedef		u64		actor_id_t;

	class xmessage;
	class xmessages;
	class xactor;
	class xworker_thread;
	class xwork;
	class xthread;

	// For messages we can have one allocator per actor for sending messages.
	// This makes the actor be able to control/limit the messages that it
	// creates and sends.
	// The necessary information for a messages is where the message came
	// from so that the receiving actor can send a message back to the
	// sender.
	class xmessage
	{
	public:
		bool				is_sender(xactor* s) const		{ return m_sender == s; }
		bool				is_recipient(xactor* r) const	{ return m_recipient == r; }

		xactor*				get_sender() const				{ return m_sender; }
		xactor*				get_recipient() const			{ return m_recipient; }

	protected:
		msg_id_t			m_id;
		xactor*				m_sender;
		xactor*				m_recipient;
	};


	class xmailbox
	{
	public:
		virtual void		send(xmessage* msg, xactor* recipient) = 0;
	};

	class xmessages
	{
	public:
		virtual s32			push(xmessage* msg) = 0;						// return==1 size was '0' before push
		virtual void		claim(u32& idx, u32& end) = 0;					// claim a message batch
		virtual void		deque(u32& idx, u32 end, xmessage*& msg) = 0;	// next message from the claimed batch
		virtual s32			release(u32 idx, u32 end) = 0;					// release batch, return 'count'
	};

	class xactor
	{
	public:
		virtual void		setmailbox(xmailbox* mailbox) = 0;
		virtual xmailbox*	getmailbox() = 0;

		virtual void		process(xmessage* msg) = 0;
		virtual void		gc(xmessage*& msg) = 0;
	};

	class xworker_thread
	{
	public:
		virtual bool		quit() const = 0;
	};

	class xwork
	{
	public:
		virtual void		add(xactor* sender, xmessage* msg, xactor* recipient) = 0;

		virtual void		queue(xactor* actor) = 0;
		virtual void		take(xworker_thread* thread, xactor*& actor, xmessage*& msg, u32& idx_begin, u32& idx_end) = 0;
		virtual void		done(xworker_thread* thread, xactor*& actor, xmessage*& msg, u32& idx_begin, u32& idx_end) = 0;
	};


	class xworker
	{
	public:
		virtual void		run(xworker_thread*, xwork*) = 0;
	};


	class xsystem
	{
	public:
		virtual void		start() = 0;
		virtual void		stop() = 0;

		virtual void		join(xactor* actor) = 0;
		virtual void		leave(xactor* actor) = 0;

		virtual void		send(xmessage* msg, xactor* recipient) = 0;
	};


	class xthread_control
	{
	public:
		virtual void		sleep(u64 time_us) = 0;
		virtual void		yield() = 0;
		virtual void		exit() = 0;
	};

	class xthread_functor
	{
	public:
		virtual s32			run(xthread_control* tc) = 0;
	};

	class xthreads
	{
	public:
		virtual void		create(xthread*&) = 0;
		virtual void		teardown(xthread*) = 0;

		virtual void		start(xthread*, xthread_functor*) = 0;
		virtual void		stop(xthread*) = 0;

		virtual void		exit() = 0;
		virtual xthread*	current() = 0;

		static void			set_instance(xthreads*);
		static xthreads*	get_instance();
	};

	class xthread
	{
	public:
		virtual u64			get_tid() const = 0;
		virtual u32			get_idx() const = 0;
		virtual void		get_name(const char*&) const = 0;
		virtual u64			get_priority() const = 0;
		virtual u64			get_stacksize() const = 0;

		virtual void*		set_tls_slot(s32 slot, void*) = 0;
		virtual void*		get_tls_slot(s32 slot) = 0;

	protected:
		virtual void		set_tid(u64 tid) = 0;
		virtual void		set_idx(u32 idx) = 0;
		virtual void		set_name(const char*) = 0;
		virtual void		set_priority(u64 size) = 0;
		virtual void		set_stacksize(u64 size) = 0;
		virtual void		set_tls_storage(s32 max_slots, void** storage) = 0;

		virtual s32			run(xthread_functor* f) = 0;
		virtual void		stop() = 0;

		virtual void		start() = 0;
	};

	class xsemaphore
	{
	public:
		virtual void		setup(s32 count) = 0;
		virtual void		teardown() = 0;

		virtual void		request() = 0;
		virtual void		release() = 0;
	};


	void				send(xactor* sender, xmessage* msg, xactor* recipient);

} // namespace xcore


#endif // __X_ACTOR_ACTOR_H__