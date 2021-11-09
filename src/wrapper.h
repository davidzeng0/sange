#pragma once
#include <napi.h>
#include <vector>
#include "ffmpeg.h"

class PlayerWrapper;

struct SecretBox{
	enum{
		NONE = 0,
		LITE,
		SUFFIX,
		DEFAULT
	};

	std::vector<uint8_t> secret_key;
	std::vector<uint8_t> buffer;

	uint8_t nonce_buffer[24];
	uint8_t random_bytes[24];
	uint8_t audio_nonce[24];

	unsigned int timestamp;
	unsigned int nonce;

	int mode;
	int ssrc;
	int message_size;

	unsigned short sequence;
};

#include "player.h"

class PlayerWrapper : public Napi::ObjectWrap<PlayerWrapper>{
private:
	Player* player;

	Napi::ObjectReference self;
	Napi::Reference<Napi::Uint8Array> buffer;

	void checkDestroyed(Napi::Env env);

	void handle_ready();
	void handle_packet();
	void handle_finish();
	void handle_error();

	SecretBox secret_box;
	AVPacket* packet;

	uv_mutex_t mutex;
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

	Napi::Value setSecretBox(const Napi::CallbackInfo& info);

	Napi::Value updateSecretBox(const Napi::CallbackInfo& info);

	Napi::Value getSecretBox(const Napi::CallbackInfo& info);

	int process_packet();

	void signal(PlayerSignal signal);
};