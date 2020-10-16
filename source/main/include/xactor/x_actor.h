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
	class xwork;
	class xworker;

	// For messages we can have one allocator per actor for sending messages.
	// This makes the actor be able to control/limit the messages that it
	// creates and sends.
	// The necessary information for a message is where the message came
	// from so that the receiving actor can send a message back to the
	// sender.
	// We base the receiving of messages on simple structs, messages are
	// always send back to the sender for garbage collection to simplify
	// creation, re-use and destruction of messages.

	msg_id_t		xmsgid(const char*);

	class xmessage
	{
	public:
		bool				is_sender(xactor* s) const		{ return m_sender == s; }
		bool				is_recipient(xactor* r) const	{ return m_recipient == r; }

		xactor*				get_sender() const				{ return m_sender; }
		xactor*				get_recipient() const			{ return m_recipient; }

		bool				has_id(msg_id_t _id) const		{ return m_id == _id; }

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

		virtual void		received(xmessage* msg) = 0;
		virtual void		returned(xmessage*& msg) = 0;
	};


	class xsystem
	{
	public:
		virtual void		start() = 0;
		virtual void		stop() = 0;

		virtual void		join(xactor* actor) = 0;
		virtual void		leave(xactor* actor) = 0;
	};

	void				send(xsystem* system, xactor* sender, xmessage* msg, xactor* recipient);

} // namespace xcore


#endif // __X_ACTOR_ACTOR_H__