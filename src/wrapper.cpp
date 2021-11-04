#include <iostream>
#include <uv.h>
#include <vector>
#include "wrapper.h"
#include "message.h"

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
		InstanceMethod<&PlayerWrapper::destroy>("destroy")
	});

	return constructor;
}

PlayerWrapper::PlayerWrapper(const Napi::CallbackInfo& info) : Napi::ObjectWrap<PlayerWrapper>(info), self(Napi::Persistent(info.This().As<Napi::Object>())){
	player = nullptr;
	player = new Player(this);
}

PlayerWrapper::~PlayerWrapper(){
	if(player)
		player -> destroy();
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
	}

	return info.Env().Undefined();
}

void PlayerWrapper::signal(PlayerSignal signal){
	if(!player)
		return;
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
	AVPacket* pkt = player -> packet;

	Napi::Object packet = Napi::Object::New(Env());
	Napi::Uint8Array array = Napi::Uint8Array::New(Env(), pkt -> size);

	memcpy(array.Data(), pkt -> data, pkt -> size);

	packet["buffer"] = array;
	packet["frame_size"] = pkt -> duration;

	self.Get("onpacket").As<Napi::Function>().Call(self.Value(), {packet});
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