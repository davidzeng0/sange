#include <iostream>
#include <uv.h>
#include <netdb.h>
#include <sodium/crypto_secretbox.h>
#include <sodium/randombytes.h>
#include "wrapper.h"
#include "message.h"

#define BUFFER_SIZE 8192

Napi::Function PlayerWrapper::init(Napi::Env env){
	Napi::Function constructor = DefineClass(env, "Player", {
		InstanceMethod<&PlayerWrapper::setURL>("setURL"),
		InstanceMethod<&PlayerWrapper::setOutput>("setOutput"),
		InstanceMethod<&PlayerWrapper::setPaused>("setPaused"),
		InstanceMethod<&PlayerWrapper::setVolume>("setVolume"),
		InstanceMethod<&PlayerWrapper::setBitrate>("setBitrate"),
		InstanceMethod<&PlayerWrapper::setRate>("setRate"),
		InstanceMethod<&PlayerWrapper::setTempo>("setTempo"),
		InstanceMethod<&PlayerWrapper::setTremolo>("setTremolo"),
		InstanceMethod<&PlayerWrapper::setEqualizer>("setEqualizer"),
		InstanceMethod<&PlayerWrapper::seek>("seek"),
		InstanceMethod<&PlayerWrapper::getTime>("getTime"),
		InstanceMethod<&PlayerWrapper::getDuration>("getDuration"),
		InstanceMethod<&PlayerWrapper::getFramesDropped>("getFramesDropped"),
		InstanceMethod<&PlayerWrapper::getTotalFrames>("getTotalFrames"),
		InstanceMethod<&PlayerWrapper::start>("start"),
		InstanceMethod<&PlayerWrapper::stop>("stop"),
		InstanceMethod<&PlayerWrapper::destroy>("destroy"),
		InstanceMethod<&PlayerWrapper::setSecretBox>("setSecretBox"),
		InstanceMethod<&PlayerWrapper::updateSecretBox>("updateSecretBox"),
		InstanceMethod<&PlayerWrapper::getSecretBox>("getSecretBox")
	});

	return constructor;
}

PlayerWrapper::PlayerWrapper(const Napi::CallbackInfo& info) : Napi::ObjectWrap<PlayerWrapper>(info), self(Napi::Persistent(info.This().As<Napi::Object>())){
	player = nullptr;
	packet = av_packet_alloc();

	if(!packet)
		throw std::bad_alloc();
	try{
		player = new Player(this);
	}catch(std::bad_alloc& e){
		av_packet_free(&packet);

		throw;
	}

	uv_mutex_init(&mutex);

	memset(secret_box.nonce_buffer, 0, sizeof(secret_box.nonce_buffer));
	memset(secret_box.audio_nonce, 0, sizeof(secret_box.audio_nonce));

	if(info.Length() > 0)
		buffer = std::move(Napi::Reference<Napi::Uint8Array>(Napi::Persistent(info[0].As<Napi::Uint8Array>())));
}

PlayerWrapper::~PlayerWrapper(){
	if(player){
		player -> destroy();

		av_packet_free(&packet);
	}
}

void PlayerWrapper::checkDestroyed(Napi::Env env){
	if(!player)
		throw Napi::Error::New(env, "Destroyed");
}

Napi::Value PlayerWrapper::setURL(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> setURL(info[0].As<Napi::String>());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setOutput(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> setFormat(info[0].As<Napi::Number>().Int32Value(), info[1].As<Napi::Number>().Int32Value(), info[2].As<Napi::Number>().Int32Value());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setPaused(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> setPaused(info[0].As<Napi::Boolean>().Value());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setVolume(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> setVolume(info[0].As<Napi::Number>().DoubleValue());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setBitrate(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> setBitrate(info[0].As<Napi::Number>().DoubleValue());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setRate(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> setRate(info[0].As<Napi::Number>().DoubleValue());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setTempo(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> setTempo(info[0].As<Napi::Number>().DoubleValue());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setTremolo(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> setTremolo(info[0].As<Napi::Number>().DoubleValue(), info[1].As<Napi::Number>().DoubleValue());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setEqualizer(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	std::vector<Equalizer> eqs(info.Length(), Equalizer());

	for(size_t i = 0; i < info.Length(); i++){
		Napi::Object obj = info[i].As<Napi::Object>();

		eqs[i].band = obj.Get("band").As<Napi::Number>().DoubleValue();
		eqs[i].gain = obj.Get("gain").As<Napi::Number>().DoubleValue();
	}

	player -> setEqualizer(eqs.data(), eqs.size());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::seek(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> seek(info[0].As<Napi::Number>().DoubleValue());

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::getTime(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	return Napi::Number::New(info.Env(), player -> getTime());
}

Napi::Value PlayerWrapper::getDuration(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	return Napi::Number::New(info.Env(), player -> getDuration());
}

Napi::Value PlayerWrapper::getFramesDropped(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	return Napi::Number::New(info.Env(), player -> getDroppedFrames());
}

Napi::Value PlayerWrapper::getTotalFrames(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	return Napi::Number::New(info.Env(), player -> getTotalFrames());
}

Napi::Value PlayerWrapper::getTotalPackets(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	return Napi::Number::New(info.Env(), player -> getTotalPackets());
}

Napi::Value PlayerWrapper::start(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	try{
		player -> start();
	}catch(PlayerException& e){
		std::string err(e.error);

		err += ": ";
		err += uv_strerror(e.code);

		throw Napi::Error::New(info.Env(), err);
	}

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::stop(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> stop();

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::destroy(const Napi::CallbackInfo& info){
	if(player){
		player -> destroy();
		player = nullptr;

		std::vector<uint8_t>().swap(secret_box.secret_key);
		std::vector<uint8_t>().swap(secret_box.buffer);

		av_packet_free(&packet);

		self.Reset();
		buffer.Reset();
	}

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setSecretBox(const Napi::CallbackInfo& info){
	Napi::Uint8Array key = info[0].As<Napi::Uint8Array>();
	Napi::Number mode = info[1].As<Napi::Number>();
	Napi::Number ssrc = info[2].As<Napi::Number>();

	uv_mutex_lock(&mutex);

	if(key.ByteLength() < 32)
		secret_box.secret_key.resize(32);
	else
		secret_box.secret_key.resize(key.ByteLength());
	if(!secret_box.buffer.size())
		secret_box.buffer.resize(BUFFER_SIZE);
	memcpy(secret_box.secret_key.data(), key.Data(), key.ByteLength());

	secret_box.buffer[0] = 0x80;
	secret_box.buffer[1] = 0x78;
	secret_box.mode = mode.Int32Value();
	secret_box.ssrc = ssrc.Int32Value();
	secret_box.sequence = 0;
	secret_box.timestamp = 0;
	secret_box.nonce = 0;

	uv_mutex_unlock(&mutex);

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::updateSecretBox(const Napi::CallbackInfo& info){
	Napi::Number sequence = info[0].As<Napi::Number>();
	Napi::Number timestamp = info[1].As<Napi::Number>();
	Napi::Number nonce = info[2].As<Napi::Number>();

	uv_mutex_lock(&mutex);

	secret_box.sequence = sequence.Uint32Value();
	secret_box.timestamp = timestamp.Uint32Value();
	secret_box.nonce = nonce.Uint32Value();

	uv_mutex_unlock(&mutex);

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::getSecretBox(const Napi::CallbackInfo& info){
	Napi::Object box = Napi::Object::New(info.Env());

	box["nonce"] = secret_box.nonce;
	box["timestamp"] = secret_box.timestamp;
	box["sequence"] = secret_box.sequence;

	return box;
}

template<class T>
static void write(void* data, T value){
	*((T*)data) = value;
}

template<typename T>
static void write(std::vector<uint8_t>& data, T value, int offset){
	write(data.data() + offset, value);
}

template<typename T>
static void write_offset(std::vector<uint8_t>& data, T value, int& offset){
	write(data, value, offset);

	offset += sizeof(T);
}

int PlayerWrapper::process_packet(){
	if(!player) abort();

	av_packet_move_ref(packet, player -> packet);

	if(!secret_box.secret_key.size())
		return 0;
	if(packet -> size > crypto_secretbox_MESSAGEBYTES_MAX)
		return AVERROR(EINVAL); /* should never happen */
	int offset = 2,
		len,
		msg_length = packet -> size + crypto_secretbox_MACBYTES;
	uint8_t* nonce;

	uv_mutex_lock(&mutex);

	secret_box.sequence++;
	secret_box.timestamp += packet -> duration;

	write_offset(secret_box.buffer, htons(secret_box.sequence), offset);
	write_offset(secret_box.buffer, htonl(secret_box.timestamp), offset);
	write_offset(secret_box.buffer, htonl(secret_box.ssrc), offset);

	switch(secret_box.mode){
		case SecretBox::LITE:
			len = 4;
			secret_box.nonce++;
			nonce = secret_box.nonce_buffer;

			if(len + msg_length + offset > secret_box.buffer.size())
				goto fail;
			write(secret_box.nonce_buffer, htonl(secret_box.nonce));
			write(secret_box.buffer, htonl(secret_box.nonce), offset + msg_length);

			break;
		case SecretBox::SUFFIX:
			len = 24;
			nonce = secret_box.random_bytes;

			if(len + msg_length + offset > secret_box.buffer.size())
				goto fail;
			randombytes_buf(secret_box.random_bytes, sizeof(secret_box.random_bytes));
			write(secret_box.buffer, secret_box.random_bytes, offset + msg_length);

			break;
		case SecretBox::DEFAULT:
		default:
			len = 0;
			nonce = secret_box.audio_nonce;

			if(len + msg_length + offset > secret_box.buffer.size())
				goto fail;
			memcpy(secret_box.audio_nonce, secret_box.buffer.data(), offset);

			break;
	}

	crypto_secretbox_easy(secret_box.buffer.data() + offset, packet -> data, packet -> size, nonce, secret_box.secret_key.data());

	secret_box.message_size = offset + msg_length + len;

	uv_mutex_unlock(&mutex);

	return 0;

	fail:

	return AVERROR_BUFFER_TOO_SMALL;
}

void PlayerWrapper::signal(PlayerSignal signal){
	if(!player) abort();

	Napi::HandleScope scope(Env());

	try{
		switch(signal){
			case PLAYER_READY:
				handle_ready();

				break;
			case PLAYER_PACKET:
				handle_packet();

				break;
			case PLAYER_FINISH:
				handle_finish();

				break;
			case PLAYER_ERROR:
				handle_error();

				break;
		}
	}catch(Napi::Error& e){
		e.ThrowAsJavaScriptException();
	}
}

void PlayerWrapper::handle_ready(){
	self.Get("onready").As<Napi::Function>().Call(self.Value(), {});
}

void PlayerWrapper::handle_packet(){
	void* data;
	int size;

	Napi::Uint8Array array;

	if(secret_box.secret_key.size()){
		data = secret_box.buffer.data();
		size = secret_box.message_size;
	}else{
		data = packet -> data;
		size = packet -> size;
	}

	if(!buffer.IsEmpty()){
		array = buffer.Value();

		if(size > array.ByteLength())
			size = array.ByteLength();
	}else{
		array = Napi::Uint8Array::New(Env(), size);
	}

	memcpy(array.Data(), data, size);

	self.Get("onpacket").As<Napi::Function>().Call(self.Value(), {array, Napi::Number::New(Env(), size), Napi::Number::New(Env(), packet -> duration)});

	av_packet_unref(packet);
}

void PlayerWrapper::handle_finish(){
	self.Get("onfinish").As<Napi::Function>().Call(self.Value(), {});
}

void PlayerWrapper::handle_error(){
	const PlayerError& err = player -> getError();

	bool retry;

	switch(err.code){
		case AVERROR_HTTP_BAD_REQUEST:
		case AVERROR_HTTP_UNAUTHORIZED:
		case AVERROR_HTTP_FORBIDDEN:
		case AVERROR_HTTP_NOT_FOUND:
		case AVERROR_HTTP_OTHER_4XX:
		case AVERROR_HTTP_SERVER_ERROR:
			retry = true;

			break;
		default:
			retry = false;

			break;
	}

	Napi::Error error = Napi::Error::New(Env(), err.error);
	Napi::Number code = Napi::Number::New(Env(), err.code);
	Napi::Boolean retryable = Napi::Boolean::New(Env(), retry);

	self.Get("onerror").As<Napi::Function>().Call(self.Value(), {error.Value(), code, retryable});
}