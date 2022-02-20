#pragma once
#include <string>
#include "ffmpeg.h"
#include "thread.h"

class Player;
struct PlayerCallbacks{
	int (*ready)(Player* player);
	int (*seeked)(Player* player);
	int (*unpaused)(Player* player);
	int (*packet)(Player* player, AVPacket* packet);
	int (*send_packet)(Player* player);
	int (*finish)(Player* player);
	void (*error)(Player* player, const std::string& error, int code);
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

class PlayerContext{
private:
	Player* list;
	Mutex mutex;

	void add(Player* player);
	void remove(Player* player);

	friend class Player;
public:
	void wait_threads();
};

class Player{
private:
	Player* next;
	Player* prev;
	PlayerContext* context;

	PlayerCallbacks* callbacks;

	struct PlayerError{
		std::string str;

		int code;
	} error;

	Thread thread;
	Cond cond;
	Mutex mutex;

	std::string url;

	bool isfile;

	bool running;
	bool destroyed;

	bool pipeline;

	bool b_stop;
	bool b_start;
	bool b_pause;
	bool b_seek;
	bool b_bitrate;
	int bitrate;
	double seek_to;

	double time;
	double time_start;
	double duration;
	long dropped_samples;
	long total_samples;
	long total_packets;

	AudioFormat audio_in, audio_out;

	AVFormatContext* format_ctx;
	AVStream* stream;
	AVPacket* packet;

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

	AVCodecID encoder_id;

	const AVCodec* encoder;
	const AVCodec* decoder;

	bool decoder_has_data;
	bool encoder_has_data;
	bool filter_has_data;

	int64_t last_pts;
	AVRational last_tb;

	static int decode_interrupt(void* p);
	static void s_player_thread(void* p);

	bool filters_neq();
	bool filters_set();
	void filters_seteq();
	int init_pipeline();
	void pipeline_destroy();
	int configure_filters();
	int read_packet();
	void run();
	void cleanup();
	void player_thread();
	bool should_run();

	template<class T>
	void wait_cond(T t);
	void signal_cond();

	template<class T>
	int callback_wrap(T t, bool run = true);

	~Player();

	friend class PlayerContext;
public:
	Mutex data_mutex;

	void* data; /* user data ptr */

	Player(PlayerContext* context, PlayerCallbacks* callbacks, void* data);

	int start();

	void setURL(std::string url, bool isfile);
	void setOutputCodec(AVCodecID codec);
	void setFormat(int channels, int sample_rate, int bitrate);

	double getTime();
	double getDuration();
	long getDroppedSamples();
	long getTotalSamples();
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

	bool isCodecCopy();
};