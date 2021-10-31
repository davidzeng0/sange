#include <stdexcept>
#include "wrapper.h"
#include "player.h"
#include "message.h"

uv_async_t* Message::async = nullptr;
Message* Message::message_head = nullptr;
uv_mutex_t Message::mutex = {};

ulong Message::num_players = 0;

bool Message::active = false;
bool Message::mutex_init = false;

static void close_cb(uv_handle_t* handle){
	free(handle);
}

void Message::async_cb(uv_async_t* async){
	Message* msg, *next;

	uv_mutex_lock(&mutex);

	msg = message_head;
	active = false;
	message_head = nullptr;

	uv_mutex_unlock(&mutex);

	if(!msg){
		uv_close((uv_handle_t*)async, close_cb);

		async = nullptr;

		return;
	}

	do{
		next = msg -> m_next;
		msg -> m_next = nullptr;
		msg -> received();
		msg = next;
	}while(msg);
}

void Message::init(){
	uv_mutex_init(&mutex);
}

int Message::inc(){
	int err = 0;

	uv_mutex_lock(&mutex);

	if(!num_players){
		async = (uv_async_t*)calloc(1, sizeof(uv_async_t));

		if(!async)
			err = UV_ENOMEM;
		else
			err = uv_async_init(uv_default_loop(), async, async_cb);
	}

	if(!err)
		num_players++;
	uv_mutex_unlock(&mutex);

	return err;
}

void Message::dec(){
	uv_mutex_lock(&mutex);

	num_players--;

	if(!num_players){
		uv_async_send(async);

		async = nullptr;
	}

	uv_mutex_unlock(&mutex);
}

Message::Message(Player* p){
	player = p;
	incd = false;
}

int Message::async_init(){
	int err = inc();

	if(err)
		return err;
	incd = true;

	return 0;
}

void Message::send(){
	bool send = false;

	uv_mutex_lock(&mutex);

	m_next = message_head;
	message_head = this;

	if(!active){
		active = true;
		send = true;
	}

	uv_mutex_unlock(&mutex);

	if(send)
		uv_async_send(async);
}

void Message::received(){
	player -> received_message();
}

Message::~Message(){
	if(incd)
		dec();
}