#pragma once
#include <napi.h>
#include "ffmpeg.h"

class PlayerWrapper;

#include "player.h"

class PlayerWrapper : public Napi::ObjectWrap<PlayerWrapper>{
private:
	Player* player;

	Napi::ObjectReference self;

	void checkDestroyed(Napi::Env env);

	void handle_ready();
	void handle_packet();
	void handle_finish();
	void handle_error();
public:
	static Napi::Function init(Napi::Env env);

	PlayerWrapper(const Napi::CallbackInfo& info);

	~PlayerWrapper();

	Napi::Value setURL(const Napi::CallbackInfo& info);

	Napi::Value setOutput(const Napi::CallbackInfo& info);

	Napi::Value setPaused(const Napi::CallbackInfo& info);

	Napi::Value setVolume(const Napi::CallbackInfo& info);

	Napi::Value setBitrate(const Napi::CallbackInfo& info);

	Napi::Value setRate(const Napi::CallbackInfo& info);

	Napi::Value setTempo(const Napi::CallbackInfo& info);

	Napi::Value setTremolo(const Napi::CallbackInfo& info);

	Napi::Value setEqualizer(const Napi::CallbackInfo& info);

	Napi::Value seek(const Napi::CallbackInfo& info);

	Napi::Value getTime(const Napi::CallbackInfo& info);

	Napi::Value getDuration(const Napi::CallbackInfo& info);

	Napi::Value getFramesDropped(const Napi::CallbackInfo& info);

	Napi::Value getTotalFrames(const Napi::CallbackInfo& info);

	Napi::Value getTotalPackets(const Napi::CallbackInfo& info);

	Napi::Value start(const Napi::CallbackInfo& info);

	Napi::Value stop(const Napi::CallbackInfo& info);

	Napi::Value destroy(const Napi::CallbackInfo& info);

	void signal(PlayerSignal signal);
};