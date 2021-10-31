#pragma once
#include <string>
#include <uv.h>
#include <exception>

class PlayerException;
class Player;

enum PlayerSignal{
	PLAYER_NONE   = 0x0,
	PLAYER_READY  = 0x1,
	PLAYER_PACKET = 0x2,
	PLAYER_FINISH = 0x4,
	PLAYER_ERROR  = 0x8
};

#include "message.h"
#include "ffmpeg.h"
#include "wrapper.h"

class PlayerException : public std::exception{
public:
	const char* error;

	int code;

	PlayerException(const char* error, int code);
};

struct AudioFormat{
	int channels;
	int sample_rate;
	int fmt;

	int64_t channel_layout;

	AudioFormat(){}

	void reset(){
		channel_layout = 0;
		fmt = 0;
	}
};

class Filter{
public:
	bool is_set() = delete;
	bool is_changed() = delete;
	void reset_change() = delete;

	void set() = delete;

	std::string to_string(AudioFormat in, AudioFormat out) = delete;
};

class FloatFilter : public Filter{
protected:
	float value;
	float new_value;
public:
	FloatFilter(){
		value = 1;
		new_value = 1;
	}

	bool is_set(){
		return new_value != 1;
	}

	bool is_changed(){
		return value != new_value;
	}

	void reset_change(){
		value = new_value;
	}

	void set(float value){
		new_value = value;
	}
};

class VolumeFilter : public FloatFilter{
public:
	std::string to_string(AudioFormat in, AudioFormat out){
		return "volume=" + std::to_string(value);
	}
};

class RateFilter : public FloatFilter{
public:
	std::string to_string(AudioFormat in, AudioFormat out){
		return "asetrate=" + std::to_string(value * in.sample_rate);
	}
};

class TempoFilter : public FloatFilter{
public:
	std::string to_string(AudioFormat in, AudioFormat out){
		return "atempo=" + std::to_string(value);
	}
};

class TremoloFilter : public Filter{
protected:
	struct FilterParams{
		double depth;
		double rate;

		FilterParams(){
			depth = 0;
			rate = 0;
		}

		bool operator!=(const FilterParams& o){
			return depth != o.depth || rate != o.rate;
		}
	};

	FilterParams value;
	FilterParams new_value;
public:
	TremoloFilter(){}

	bool is_set(){
		return new_value.depth != 0 && new_value.rate != 0;
	}

	bool is_changed(){
		return value != new_value;
	}

	void reset_change(){
		value = new_value;
	}

	void set(float depth, float rate){
		new_value.depth = depth;
		new_value.rate = rate;
	}

	std::string to_string(AudioFormat in, AudioFormat out){
		return "tremolo=f=" + std::to_string(value.rate) + ":d=" + std::to_string(value.depth);
	}
};

struct Equalizer{
	double band;
	double gain;
};

class EqualizerFilter : public Filter{
protected:
	std::string filter;

	bool b_set;
	bool b_changed;
public:
	EqualizerFilter(){
		b_set = false;
		b_changed = false;
	}

	void set(Equalizer* eqs, size_t length){
		filter.clear();

		if(b_set || length)
			b_changed = true;
		b_set = false;

		if(!length)
			return;
		filter += "firequalizer=gain_entry='";

		for(size_t i = 0; i < length; i++){
			filter += "entry(" + std::to_string(eqs[i].band) + "," + std::to_string(eqs[i].gain);

			if(i == length - 1)
				filter += ")";
			else
				filter += ");";
		}

		filter += "'";
		b_set = true;
	}

	bool is_set(){
		return b_set;
	}

	bool is_changed(){
		return b_changed;
	}

	void reset_change(){
		b_changed = false;
	}

	std::string to_string(AudioFormat in, AudioFormat out){
		return filter;
	}
};

struct PlayerError{
	std::string error;

	int code;
};

class Player{
private:
	PlayerWrapper* wrapper;

	Message message;
	PlayerError error;

	uv_thread_t thread;
	uv_cond_t cond;
	uv_mutex_t mutex;

	std::string url;

	bool running;
	bool destroyed;
	bool queued;
	bool waiting;

	bool pipeline;

	bool b_stop;
	bool b_start;
	bool pause;
	bool b_seek;
	bool b_bitrate;
	int bitrate;
	double seek_to;

	double time;
	double time_start;
	double duration;
	long dropped_frames;
	long total_frames;
	long total_packets;

	AudioFormat audio_in, audio_out;

	AVFormatContext* format_ctx;
	AVStream* stream;

	AVFilterGraph* filter_graph;
	AVFilterContext* filter_src;
	AVFilterContext* filter_sink;

	/* filters */

	VolumeFilter volume;
	RateFilter rate;
	TempoFilter tempo;
	TremoloFilter tremolo;
	EqualizerFilter equalizer;

	/* end filters */

	AVCodecContext* decoderctx;
	AVCodecContext* encoderctx;
	AVFrame* frame;

	const AVCodec* encoder;
	const AVCodec* decoder;

	bool decoder_has_data;
	bool encoder_has_data;
	bool filter_has_data;

	int64_t last_pts;
	AVRational last_tb;

	PlayerSignal signal;

	static int decode_interrupt(void* p);
	static void s_player_thread(void* p);

	bool filters_neq();
	bool filters_set();
	void filters_seteq();
	int init_pipeline();
	void pipeline_deinit();
	int configure_filters();
	int read_packet();
	void run();
	void cleanup();
	void player_thread();

	template<class T>
	void wait_cond(T t);

	void signal_cond();
	void send_message(PlayerSignal signal);
	void send_error(int err);
	void received_message();

	friend class Message;
public:
	AVPacket* packet;

	Player(PlayerWrapper* wrapper);

	void start();

	void setURL(std::string url);
	void setFormat(int channels, int sample_rate, int bitrate);

	double getTime();
	double getDuration();
	long getDroppedFrames();
	long getTotalFrames();
	long getTotalPackets();

	void setPaused(bool paused);
	void seek(double time);
	void setBitrate(int bitrate);

	void setVolume(float volume);
	void setRate(float rate);
	void setTempo(float tempo);
	void setTremolo(float depth, float rate);
	void setEqualizer(Equalizer* eqs, size_t length);

	void stop();
	void destroy();

	const PlayerError& getError();

	~Player();
};