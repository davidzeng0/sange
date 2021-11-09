#pragma once
#include <napi.h>
#include <uv.h>
#include "ffmpeg.h"

class Message;

#include "player.h"

class Message{
private:
	static int inc();
	static void dec();

	Player* player;
	Message* m_next;

	bool initialized;

	static void async_cb(uv_async_t* async);
public:
	static void init();

	Message(Player* p);
	~Message();

	int async_init();

	void send();
	void wait();
	void received();
};