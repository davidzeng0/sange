#include <uv.h>
#include <netdb.h>
#include <unistd.h>
#include <sodium/crypto_secretbox.h>
#include <sodium/randombytes.h>
#include <arpa/inet.h>
#include "wrapper.h"

PlayerCallbacks PlayerWrapper::callbacks = {
	PlayerWrapper::player_ready,
	PlayerWrapper::player_seeked,
	PlayerWrapper::player_seeked,
	PlayerWrapper::player_packet,
	PlayerWrapper::player_send_packet,
	PlayerWrapper::player_finish,
	PlayerWrapper::player_error
};

enum{
	BUFFER_SIZE = 8192
};

enum MessageType{
	MESSAGE_NONE = 0,
	MESSAGE_READY,
	MESSAGE_PACKET,
	MESSAGE_FINISH,
	MESSAGE_ERROR
};

Napi::Function PlayerWrapper::init(Napi::Env env){
	Napi::Function constructor = DefineClass(env, "FFPlayer", {
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
		InstanceMethod<&PlayerWrapper::getSecretBox>("getSecretBox"),
		InstanceMethod<&PlayerWrapper::pipe>("pipe"),
		InstanceMethod<&PlayerWrapper::isCodecCopy>("isCodecCopy"),
		InstanceMethod<&PlayerWrapper::send>("send")
	});

	return constructor;
}

#define containerof(struct, field, addr) ((struct*)((uintptr_t)(addr) - offsetof(struct, field)))

class AddonContext{
public:
	MessageContext message;
	PlayerContext player;

	bool closing;

	AddonContext(uv_loop_t* loop): message(loop){
		closing = false;
	}

	void closed(){
		if(!closing || message.count())
			return;
		player.wait_threads();

		free(this);
	}

	void close(){
		closing = true;

		closed();
	}
};

static void finalizer(Napi::Env env, AddonContext* context){
	context -> close();
}

static AddonContext* create_context(Napi::Env env){
	AddonContext* context;

	context = env.GetInstanceData<AddonContext>();

	if(context)
		return context;
	uv_loop_t* loop;

	if(napi_get_uv_event_loop(env, &loop) != napi_ok)
		throw Napi::Error::New(env, "Could not get event loop");
	context = (AddonContext*)calloc(1, sizeof(AddonContext));

	if(!context)
		throw Napi::Error::New(env, "Out of memory");
	new (context) AddonContext(loop);

	env.SetInstanceData<AddonContext, finalizer>(context);

	return context;
}

int PlayerWrapper::player_ready(Player* player){
	int err = AVERROR_EXIT;

	PlayerWrapper* wrapper;

	player -> data_mutex.lock();
	wrapper = (PlayerWrapper*)player -> data;

	if(wrapper){
		wrapper -> packet_emitted = false;
		err = wrapper -> send_message(MESSAGE_READY);
	}

	player -> data_mutex.unlock();

	return err;
}

int PlayerWrapper::player_seeked(Player* player){
	int err = 0;

	PlayerWrapper* wrapper;

	player -> data_mutex.lock();
	wrapper = (PlayerWrapper*)player -> data;

	if(!wrapper)
		err = AVERROR_EXIT;
	else
		wrapper -> packet_emitted = false;
	player -> data_mutex.unlock();

	return err;
}

int PlayerWrapper::player_packet(Player* player, AVPacket* packet){
	int err = AVERROR_EXIT;

	PlayerWrapper* wrapper;

	player -> data_mutex.lock();
	wrapper = (PlayerWrapper*)player -> data;

	if(wrapper){
		err = wrapper -> process_packet(packet);

		if(err){
			char buf[256];

			av_strerror(err, buf, sizeof(buf));

			wrapper -> error.clear();
			wrapper -> error += buf;
			wrapper -> error_code = err;
			wrapper -> send_message(MESSAGE_ERROR);

			err = AVERROR_EXIT;
		}
	}

	player -> data_mutex.unlock();

	return err;
}

int PlayerWrapper::player_send_packet(Player* player){
	int err = AVERROR_EXIT;

	PlayerWrapper* wrapper;

	player -> data_mutex.lock();
	wrapper = (PlayerWrapper*)player -> data;

	if(wrapper){
		if(wrapper -> ext_send){
			err = wrapper -> send_packet();

			if(err){
				char buf[256];

				av_strerror(err, buf, sizeof(buf));

				wrapper -> error.clear();
				wrapper -> error += buf;
				wrapper -> error_code = err;
				wrapper -> send_message(MESSAGE_ERROR);

				err = AVERROR_EXIT;
			}else if(!wrapper -> packet_emitted){
				wrapper -> packet_emitted = true;
				err = wrapper -> send_message(MESSAGE_PACKET);
			}
		}else{
			err = wrapper -> send_message(MESSAGE_PACKET);
		}
	}

	player -> data_mutex.unlock();

	return err;
}

int PlayerWrapper::player_finish(Player* player){
	int err = AVERROR_EXIT;

	PlayerWrapper* wrapper;

	player -> data_mutex.lock();
	wrapper = (PlayerWrapper*)player -> data;

	if(wrapper){
		wrapper -> packet_emitted = false;
		err = wrapper -> send_message(MESSAGE_FINISH);
	}

	player -> data_mutex.unlock();

	return err;
}

void PlayerWrapper::player_error(Player* player, const std::string& error, int code){
	PlayerWrapper* wrapper;

	player -> data_mutex.lock();
	wrapper = (PlayerWrapper*)player -> data;

	if(wrapper){
		wrapper -> error.clear();
		wrapper -> error += error;
		wrapper -> error_code = code;
		wrapper -> send_message(MESSAGE_ERROR);
	}

	player -> data_mutex.unlock();
}

int PlayerWrapper::send_message(int type){
	Player* player = this -> player;

	bool sent = false;

	message_type = type;
	message.send();
	mutex.lock();
	player -> data_mutex.unlock();
	message.wait();
	player -> data_mutex.lock();
	mutex.unlock();

	return player -> data ? 0 : AVERROR_EXIT;
}

void PlayerWrapper::handle_message(){
	Napi::HandleScope scope(Env());

	try{
		switch(message_type){
			case MESSAGE_READY:
				handle_ready();

				break;
			case MESSAGE_PACKET:
				handle_packet();

				break;
			case MESSAGE_FINISH:
				handle_finish();

				break;
			case MESSAGE_ERROR:
				handle_error(error, error_code);

				break;
		}
	}catch(Napi::Error& e){
		try{
			e.ThrowAsJavaScriptException();
		}catch(Napi::Error& e){
			/* already throwing an exception */
		}
	}
}

PlayerWrapper::PlayerWrapper(const Napi::CallbackInfo& info):
	Napi::ObjectWrap<PlayerWrapper>(info), self(Napi::Persistent(info.This().As<Napi::Object>())),
	context(create_context(info.Env())),
	message(this, &context -> message){
	player = nullptr;
	fd = -1;
	ext_send = false;
	packet_emitted = false;

	packet = av_packet_alloc();

	if(!packet)
		/* if we're really out of memory, let node js handle it */
		throw Napi::Error::New(Env(), "Out of memory");
	try{
		error.reserve(256);

		player = new Player(&context -> player, &callbacks, this);
	}catch(std::bad_alloc& e){
		av_packet_free(&packet);

		throw Napi::Error::New(Env(), "Out of memory");
	}

	memset(secret_box.nonce_buffer, 0, sizeof(secret_box.nonce_buffer));
	memset(secret_box.audio_nonce, 0, sizeof(secret_box.audio_nonce));

	if(info.Length() > 0)
		buffer = std::move(Napi::Reference<Napi::Uint8Array>(Napi::Persistent(info[0].As<Napi::Uint8Array>())));
}

PlayerWrapper::~PlayerWrapper(){
	do_destroy();
}

void PlayerWrapper::checkDestroyed(Napi::Env env){
	if(!player)
		throw Napi::Error::New(env, "Destroyed");
}

Napi::Value PlayerWrapper::setURL(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	bool isfile = false;

	if(info.Length() > 1 && info[1].As<Napi::Boolean>().Value())
		isfile = true;
	player -> setURL(info[0].As<Napi::String>(), isfile);

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setOutput(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> setOutputCodec(AV_CODEC_ID_OPUS);
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

	return Napi::Number::New(info.Env(), player -> getDroppedSamples() / 960);
}

Napi::Value PlayerWrapper::getTotalFrames(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	return Napi::Number::New(info.Env(), player -> getTotalSamples() / 960);
}

Napi::Value PlayerWrapper::getTotalPackets(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	return Napi::Number::New(info.Env(), player -> getTotalPackets());
}

Napi::Value PlayerWrapper::start(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	int err = message.init();

	if(err)
		throw Napi::Error::New(info.Env(), "Failed to create uv_async_t");
	err = player -> start();

	if(err){
		std::string str("Could not start thread: ");

		str += uv_strerror(err);

		throw Napi::Error::New(info.Env(), str);
	}

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::stop(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	player -> stop();

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::destroy(const Napi::CallbackInfo& info){
	do_destroy();

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::setSecretBox(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	Napi::Uint8Array key = info[0].As<Napi::Uint8Array>();
	Napi::Number mode = info[1].As<Napi::Number>();
	Napi::Number ssrc = info[2].As<Napi::Number>();

	secretbox.lock();

	try{
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
	}catch(std::bad_alloc& e){
		secretbox.unlock();

		throw Napi::Error::New(info.Env(), "Out of memory");
	}

	secretbox.unlock();

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::pipe(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	int fd = -1, port;

	std::string ip;
	std::string errorstr;

	ext_send = true;

	secretbox.lock();

	if(this -> fd != -1){
		close(this -> fd);

		this -> fd = -1;
	}

	secretbox.unlock();

	if(info.Length()){
		ip = info[0].As<Napi::String>().Utf8Value();
		port = info[1].As<Napi::Number>().Int32Value();
	}else{
		return info.Env().Undefined();
	}

	int family;

	union{
		in_addr in;
		in6_addr in6;
	};

	union{
		sockaddr addr;
		sockaddr_in inaddr;
		sockaddr_in6 in6addr;
	};

	if(inet_pton(AF_INET, ip.c_str(), &in) == 1)
		family = AF_INET;
	else if(inet_pton(AF_INET6, ip.c_str(), &in6) == 1)
		family = AF_INET6;
	else
		throw Napi::Error::New(info.Env(), "Invalid IP address");
	fd = socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);

	if(fd < 0)
		goto socket;
	if(family == AF_INET){
		memset(&inaddr, 0, sizeof(inaddr));

		inaddr.sin_family = AF_INET;

		if(bind(fd, &addr, sizeof(inaddr)) < 0)
			goto bind;
		inaddr.sin_port = htons(port);
		inaddr.sin_addr = in;

		if(connect(fd, &addr, sizeof(inaddr)) < 0)
			goto connect;
	}else{
		memset(&in6addr, 0, sizeof(in6addr));

		in6addr.sin6_family = AF_INET6;

		if(bind(fd, &addr, sizeof(in6addr)) < 0)
			goto bind;
		in6addr.sin6_port = htons(port);
		in6addr.sin6_addr = in6;

		if(connect(fd, &addr, sizeof(in6addr)) < 0)
			goto connect;
	}

	secretbox.lock();

	this -> fd = fd;

	secretbox.unlock();

	return info.Env().Undefined();

	socket:

	errorstr += "Could not create socket: ";

	goto err;

	bind:

	close(fd);

	errorstr += "Could not bind socket: ";

	goto err;

	connect:

	close(fd);

	errorstr += "Could not connect socket: ";

	err:

	errorstr += strerror(errno);

	throw Napi::Error::New(info.Env(), errorstr);
}

Napi::Value PlayerWrapper::updateSecretBox(const Napi::CallbackInfo& info){
	Napi::Number sequence = info[0].As<Napi::Number>();
	Napi::Number timestamp = info[1].As<Napi::Number>();
	Napi::Number nonce = info[2].As<Napi::Number>();

	secretbox.lock();

	secret_box.sequence = sequence.Uint32Value();
	secret_box.timestamp = timestamp.Uint32Value();
	secret_box.nonce = nonce.Uint32Value();

	secretbox.unlock();

	return info.Env().Undefined();
}

Napi::Value PlayerWrapper::getSecretBox(const Napi::CallbackInfo& info){
	Napi::Object box = Napi::Object::New(info.Env());

	box["nonce"] = secret_box.nonce;
	box["timestamp"] = secret_box.timestamp;
	box["sequence"] = secret_box.sequence;

	return box;
}

Napi::Value PlayerWrapper::isCodecCopy(const Napi::CallbackInfo& info){
	checkDestroyed(info.Env());

	return Napi::Boolean::New(info.Env(), player -> isCodecCopy());
}

Napi::Value PlayerWrapper::send(const Napi::CallbackInfo& info){
	return info.Env().Undefined();
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

int PlayerWrapper::process_packet(AVPacket* player_packet){
	av_packet_unref(packet);
	av_packet_move_ref(packet, player_packet);

	if(!secret_box.secret_key.size())
		return 0;
	if(packet -> size > crypto_secretbox_MESSAGEBYTES_MAX)
		return AVERROR_EXIT; /* should never happen */
	int offset = 2,
		len,
		msg_length = packet -> size + crypto_secretbox_MACBYTES;
	int err = 0;
	uint8_t* nonce;

	secretbox.lock();

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

	secretbox.unlock();

	return err;

	fail:

	return AVERROR_BUFFER_TOO_SMALL;
}

int PlayerWrapper::send_packet(){
	if(!ext_send)
		return 0;
	int err = 0;

	secretbox.lock();

	if(fd >= 0 && ::send(fd, secret_box.buffer.data(), secret_box.message_size, MSG_DONTWAIT | MSG_NOSIGNAL) < 0)
		err = AVERROR(errno);
	secretbox.unlock();

	return err;
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

	int64_t duration = packet -> duration;

	memcpy(array.Data(), data, size);
	av_packet_unref(packet);

	self.Get("onpacket").As<Napi::Function>().Call(self.Value(), {array, Napi::Number::New(Env(), size), Napi::Number::New(Env(), duration)});
}

void PlayerWrapper::handle_finish(){
	self.Get("onfinish").As<Napi::Function>().Call(self.Value(), {});
}

void PlayerWrapper::handle_error(std::string& str, int err_code){
	bool retry;

	switch(err_code){
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

	Napi::Error error = Napi::Error::New(Env(), str);
	Napi::Number code = Napi::Number::New(Env(), err_code);
	Napi::Boolean retryable = Napi::Boolean::New(Env(), retry);

	self.Get("onerror").As<Napi::Function>().Call(self.Value(), {error.Value(), code, retryable});
}

void PlayerWrapper::do_destroy(){
	if(!player)
		return;
	player -> data_mutex.lock();
	player -> data = nullptr;
	message.destroy();
	player -> data_mutex.unlock();
	player -> destroy();
	mutex.lock();
	mutex.unlock();
	player = nullptr;

	if(fd != -1)
		close(fd);

	std::vector<uint8_t>().swap(secret_box.secret_key);
	std::vector<uint8_t>().swap(secret_box.buffer);

	av_packet_free(&packet);

	self.Reset();
	buffer.Reset();
	context -> closed();
}