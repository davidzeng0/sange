#include "wrapper.h"
#include "message.h"

void MessageContext::async_cb(uv_async_t* async){
	Message* msg, *next;
	MessageContext* ctx = (MessageContext*)async -> data;

	ctx -> mutex.lock();
	msg = ctx -> message_head;
	ctx -> active = false;
	ctx -> message_head = nullptr;
	ctx -> mutex.unlock();

	while(msg){
		for(int i = 0; i < 10000 && msg; i++){
			next = msg -> next;
			msg -> prev = nullptr;
			msg -> next = nullptr;
			msg -> received();
			msg = next;
		}

		ctx -> wait_mutex.lock();
		ctx -> wait_cond.broadcast();
		ctx -> wait_mutex.unlock();
	}
}

int MessageContext::inc(Message* message){
	int err;

	if(active_messages + 1 == 0)
		return UV_ENOMEM;
	if(!active_messages){
		uv_async_t* asyn = (uv_async_t*)calloc(1, sizeof(uv_async_t));

		if(!asyn)
			return UV_ENOMEM;
		asyn -> data = this;
		err = uv_async_init(loop, asyn, async_cb);

		if(err){
			free(asyn);

			return err;
		}

		async = asyn;
	}

	active_messages++;

	return 0;
}

void MessageContext::dec(Message* message){
	if(message -> sending){
		message -> sending = false;

		mutex.lock();

		if(message == message_head){
			message_head = message -> next;

			if(message_head)
				message_head -> prev = nullptr;
		}else if(message -> prev){
			message -> prev -> next = message -> next;

			if(message -> next)
				message -> next -> prev = message -> prev;
		}

		mutex.unlock();

		wakeup(message);
	}

	if(--active_messages)
		return;
	if(async){
		mutex.lock();

		uv_close((uv_handle_t*)async, nullptr);

		async = nullptr;
		mutex.unlock();
	}
}

void MessageContext::send(Message* message){
	mutex.lock();

	if(async){
		if(message_head)
			message_head -> prev = message;
		message -> next = message_head;
		message_head = message;
		message -> sending = true;

		if(!active){
			active = true;

			uv_async_send(async);
		}
	}

	mutex.unlock();
}

void MessageContext::wait(Message* message){
	wait_mutex.lock();

	while(message -> sending)
		wait_cond.wait(wait_mutex);
	wait_mutex.unlock();
}

void MessageContext::wakeup(Message* message){
	wait_mutex.lock();
	wait_cond.broadcast();
	wait_mutex.unlock();
}

ulong MessageContext::count(){
	return active_messages;
}

MessageContext::MessageContext(uv_loop_t* l){
	loop = l;
}

Message::Message(MessageHandler* h, MessageContext* c){
	handler = h;
	context = c;
	initialized = false;
	sending = false;
	next = nullptr;
	prev = nullptr;
}

int Message::init(){
	if(initialized)
		return 0;
	int err = context -> inc(this);

	if(!err)
		initialized = true;
	return err;
}

void Message::destroy(){
	if(initialized){
		context -> dec(this);
		initialized = false;
	}
}

void Message::send(){
	context -> send(this);
}

void Message::wait(){
	if(!sending)
		return;
	context -> wait(this);
}

void Message::received(){
	handler -> handle_message();
	sending = false;
}

Message::~Message(){
	destroy();
}