#pragma once
#include <napi.h>
#include <vector>
#include "ffmpeg.h"

class PlayerWrapper;

#include "player.h"
#include "message.h"
#include "thread.h"

class AddonContext;
class PlayerWrapper : public Napi::ObjectWrap<PlayerWrapper>, public MessageHandler{
private:
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

	Player* player;

	Napi::ObjectReference self;
	Napi::Reference<Napi::Uint8Array> buffer;

	void checkDestroyed(Napi::Env env);

	void handle_ready();
	void handle_packet();
	void handle_finish();
	void handle_error(std::string& str, int err_code);
	void do_destroy();

	static int player_ready(Player* player);
	static int player_seeked(Player* player);
	static int player_packet(Player* player, AVPacket* packet);
	static int player_send_packet(Player* player);
	static int player_finish(Player* player);
	static void player_error(Player* player, const std::string& error, int code);

	static PlayerCallbacks callbacks;

	SecretBox secret_box;

	int fd;

	std::string error;
	int error_code;

	bool ext_send;
	bool packet_emitted;

	AVPacket* packet;

	Mutex mutex;
	Mutex secretbox;
	AddonContext* context;
	Message message;

	int message_type;

	int process_packet(AVPacket* packet);
	int send_packet();
	int send_message(int type);
	void handle_message();

	friend class Message;
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

	Napi::Value pipe(const Napi::CallbackInfo& info);

	Napi::Value isCodecCopy(const Napi::CallbackInfo& info);

	Napi::Value send(const Napi::CallbackInfo& info);
};