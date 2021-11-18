#include <stdexcept>
#include "wrapper.h"
#include "player.h"
#include "message.h"

static uv_async_t* async = nullptr;
static Message* message_head = nullptr;
static uv_mutex_t mutex;
static uv_cond_t cond;

static uv_mutex_t wait_mutex;
static uv_cond_t wait_cond;

static ulong num_players = 0;

static bool active = false;

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

		return;
	}

	while(msg){
		for(int i = 0; i < 10000 && msg; i++){
			next = msg -> m_next;
			msg -> received();
			msg = next;
		}

		uv_mutex_lock(&wait_mutex);
		uv_cond_broadcast(&wait_cond);
		uv_mutex_unlock(&wait_mutex);
	}
}

void Message::init(){
	uv_mutex_init(&mutex);
	uv_cond_init(&cond);
	uv_mutex_init(&wait_mutex);
	uv_cond_init(&wait_cond);
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
	initialized = false;
	sending = false;
}

int Message::async_init(){
	int err = inc();

	if(err)
		return err;
	initialized = true;

	return 0;
}

void Message::send(){
	bool send = false;

	wait();

	sending = true;

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

void Message::wait(){
	if(!sending)
		return;
	uv_mutex_lock(&wait_mutex);

	while(sending)
		uv_cond_wait(&wait_cond, &wait_mutex);
	uv_mutex_unlock(&wait_mutex);
}

void Message::received(){
	player -> received_message();
	sending = false;
}

Message::~Message(){
	if(initialized)
		dec();
}