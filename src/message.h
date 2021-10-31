#pragma once
#include <napi.h>
#include <uv.h>
#include "ffmpeg.h"

class Message;

#include "player.h"

class Message{
private:
	static uv_async_t* async;
	static Message* message_head;
	static ulong num_players;
	static uv_mutex_t mutex;
	static bool active;
	static bool mutex_init;

	static int inc();
	static void dec();

	bool incd;

	Player* player;
	Message* m_next;

	static void async_cb(uv_async_t* async);
public:
	static void init();

	Message(Player* p);
	~Message();

	int async_init();

	void send();
	void received();
};