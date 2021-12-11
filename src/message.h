#pragma once
#include <uv.h>
#include "thread.h"

class MessageHandler{
public:
	virtual void handle_message() = 0;
};

class Message;
class MessageContext{
private:
	uv_async_t* async;
	uv_loop_t* loop;
	Message* message_head;

	Mutex mutex;
	Cond cond;
	Mutex wait_mutex;
	Cond wait_cond;

	ulong active_messages;

	bool active;

	static void async_cb(uv_async_t* async);
	int inc(Message* message);
	void dec(Message* message);
	void send(Message* message);
	void wait(Message* message);
	void wakeup(Message* message);

	friend class Message;
public:
	MessageContext(uv_loop_t* loop);

	ulong count();
};

class Message{
private:
	Message* next;
	Message* prev;
	MessageContext* context;
	MessageHandler* handler;

	bool initialized;
	bool sending;

	friend class MessageContext;

	void received();
public:
	Message(MessageHandler* handler, MessageContext* context);
	~Message();

	int init();
	void destroy();
	void send();
	void wait();
};