#include <iostream>
#include <time.h>
#include <unistd.h>
#include <opus/opus.h>
#include "player.h"
#include "message.h"

const static int PLAYER_OUTPUT_CODEC = AV_CODEC_ID_OPUS;

PlayerException::PlayerException(const char* _error, int _code){
	error = _error;
	code = _code;
}

int Player::decode_interrupt(void* p){
	Player* player = (Player*)p;

	return player -> destroyed;
}

void Player::s_player_thread(void* p){
	Player* player = (Player*)p;

	player -> player_thread();
}

bool Player::filters_neq(){
	return volume.is_changed() || rate.is_changed() || tempo.is_changed() || tremolo.is_changed() || equalizer.is_changed();
}

bool Player::filters_set(){
	return volume.is_set() || rate.is_set() || tempo.is_set() || tremolo.is_set() || equalizer.is_set();
}

void Player::filters_seteq(){
	volume.reset_change();
	rate.reset_change();
	tempo.reset_change();
	tremolo.reset_change();
	equalizer.reset_change();
}

int Player::init_pipeline(){
	int err;

	decoderctx = avcodec_alloc_context3(nullptr);
	encoderctx = avcodec_alloc_context3(nullptr);

	if(!decoderctx || !encoderctx){
		err = AVERROR(ENOMEM);

		goto end;
	}

	if((err = avcodec_parameters_to_context(decoderctx, stream -> codecpar)) < 0)
		goto end;
	decoderctx -> pkt_timebase = stream -> time_base;
	decoder = avcodec_find_decoder(decoderctx -> codec_id);
	decoderctx -> request_sample_fmt = AV_SAMPLE_FMT_S16;

	if((err = avcodec_open2(decoderctx, decoder, nullptr)) < 0)
		goto end;
	audio_out.channel_layout = av_get_default_channel_layout(audio_out.channels);

	encoderctx -> bit_rate = bitrate;
	encoderctx -> sample_rate = audio_out.sample_rate;
	encoderctx -> channels = audio_out.channels;
	encoderctx -> sample_fmt = AV_SAMPLE_FMT_FLT;
	encoderctx -> channel_layout = audio_out.channel_layout;
	encoderctx -> compression_level = 10;

	if((err = avcodec_open2(encoderctx, encoder, nullptr)) < 0)
		goto end;
	audio_in.channels = decoderctx -> channels;
	audio_in.sample_rate = decoderctx -> sample_rate;
	audio_in.fmt = decoderctx -> sample_fmt;
	audio_in.channel_layout = 0;

	audio_out.fmt = encoderctx -> sample_fmt;

	last_pts = AV_NOPTS_VALUE;
	last_tb = {0, 1};

	pipeline = true;

	return 0;

	end:

	avcodec_free_context(&decoderctx);
	avcodec_free_context(&encoderctx);

	return err;
}

void Player::pipeline_deinit(){
	avfilter_graph_free(&filter_graph);
	avcodec_free_context(&decoderctx);
	avcodec_free_context(&encoderctx);

	pipeline = false;
}

int Player::configure_filters(){
	AVFilterInOut *outputs = nullptr, *inputs = nullptr;

	avfilter_graph_free(&filter_graph);

	filter_src = nullptr;
	filter_sink = nullptr;

	filter_graph = avfilter_graph_alloc();

	if(!filter_graph)
		return AVERROR(ENOMEM);
	filter_graph -> nb_threads = 1;

	int64_t channel_layouts[] = {audio_out.channel_layout, -1};
	int sample_rates[] = {audio_out.sample_rate, -1};
	int channels[] = {audio_out.channels, -1};
	int sample_fmts[] = {audio_out.fmt, -1};

	char filter_args[256];

	snprintf(filter_args, sizeof(filter_args), "sample_rate=%d:sample_fmt=%d:channels=%d:channel_layout=0x%" PRIx64,
												audio_in.sample_rate, audio_in.fmt, audio_in.channels, audio_in.channel_layout);
	int ret;

	if((ret = avfilter_graph_create_filter(&filter_src, avfilter_get_by_name("abuffer"), nullptr, filter_args, nullptr, filter_graph)) < 0)
		goto failfilter;
	if((ret = avfilter_graph_create_filter(&filter_sink, avfilter_get_by_name("abuffersink"), nullptr, nullptr, nullptr, filter_graph)) < 0)
		goto failfilter;
	if((ret = av_opt_set_int(filter_sink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto failfilter;
	if((ret = av_opt_set_int_list(filter_sink, "sample_fmts", sample_fmts, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto failfilter;
	if((ret = av_opt_set_int_list(filter_sink, "channel_layouts", channel_layouts, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto failfilter;
	if((ret = av_opt_set_int_list(filter_sink, "channel_counts", channels, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto failfilter;
	if((ret = av_opt_set_int_list(filter_sink, "sample_rates", sample_rates, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto failfilter;
	if(filters_set()){
		outputs = avfilter_inout_alloc();
		inputs = avfilter_inout_alloc();

		if(!outputs || !inputs){
			ret = AVERROR(ENOMEM);

			goto failgraph;
		}

		outputs -> name = av_strdup("in");
		outputs -> filter_ctx = filter_src;
		outputs -> pad_idx = 0;
		outputs -> next = nullptr;

		inputs -> name = av_strdup("out");
		inputs -> filter_ctx = filter_sink;
		inputs -> pad_idx = 0;
		inputs -> next = nullptr;

		uv_mutex_lock(&mutex);

		ret = 0;

		try{
			std::string filter("");

			if(volume.is_set())
				filter += "," + volume.to_string(audio_in, audio_out);
			if(rate.is_set())
				filter += "," + rate.to_string(audio_in, audio_out);
			if(tempo.is_set())
				filter += "," + tempo.to_string(audio_in, audio_out);
			if(tremolo.is_set())
				filter += "," + tremolo.to_string(audio_in, audio_out);
			if(equalizer.is_set())
				filter += "," + equalizer.to_string(audio_in, audio_out);
			ret = avfilter_graph_parse_ptr(filter_graph, filter.c_str() + 1, &inputs, &outputs, nullptr);
		}catch(std::bad_alloc& e){
			ret = AVERROR(ENOMEM);
		}

		uv_mutex_unlock(&mutex);

		if(ret < 0)
			goto failgraph;
	}else if((ret = avfilter_link(filter_src, 0, filter_sink, 0)) < 0)
		goto failfilter;
	if((ret = avfilter_graph_config(filter_graph, nullptr)) < 0)
		goto failgraph;
	av_buffersink_set_frame_size(filter_sink, encoderctx -> frame_size);

	return 0;

	failgraph:

	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);

	failfilter:

	avfilter_graph_free(&filter_graph);

	return ret;
}

int Player::read_packet(){
	int err;

	while(!b_stop){
		if(encoder_has_data){
			err = avcodec_receive_packet(encoderctx, packet);

			if(err == AVERROR(EAGAIN))
				encoder_has_data = false;
			else if(err)
				return err;
			else
				break;
		}

		if(filter_has_data){
			err = av_buffersink_get_frame(filter_sink, frame);

			if(err == AVERROR(EAGAIN))
				filter_has_data = false;
			else if(err)
				return err;
			else{
				err = avcodec_send_frame(encoderctx, frame);

				if(err) return err;

				encoder_has_data = true;

				continue;
			}
		}

		if(decoder_has_data){
			err = avcodec_receive_frame(decoderctx, frame);

			if(err == AVERROR(EAGAIN))
				decoder_has_data = false;
			else if(err)
				return err;
			else{
				AVRational tb = {1, frame -> sample_rate};

				if(frame -> pts != AV_NOPTS_VALUE)
					frame -> pts = av_rescale_q(frame -> pts, decoderctx -> pkt_timebase, tb);
				else if(last_pts != AV_NOPTS_VALUE)
					frame -> pts = av_rescale_q(last_pts, last_tb, tb);
				if(frame -> pts != AV_NOPTS_VALUE){
					last_pts = frame -> pts + frame -> nb_samples;
					last_tb = tb;
				}

				bool channel_fmt_neq = false;

				if(frame -> channels != audio_in.channels){
					channel_fmt_neq = true;
				}else if(audio_in.channels == 1){
					if(av_get_packed_sample_fmt((AVSampleFormat)frame -> format) != av_get_packed_sample_fmt((AVSampleFormat)audio_in.fmt))
						channel_fmt_neq = true;
				}else if(frame -> format != audio_in.fmt){
					channel_fmt_neq = true;
				}

				if(channel_fmt_neq || frame -> sample_rate != audio_in.sample_rate || frame -> channel_layout != audio_in.channel_layout || filters_neq()){
					audio_in.fmt = frame -> format;
					audio_in.channels = frame -> channels;
					audio_in.channel_layout = frame -> channel_layout;
					audio_in.sample_rate = frame -> sample_rate;

					filters_seteq();

					err = configure_filters();

					if(err)
						return err;
				}

				if(filter_graph){
					err = av_buffersrc_add_frame(filter_src, frame);
					filter_has_data = true;
				}else{
					err = avcodec_send_frame(encoderctx, frame);
					encoder_has_data = true;
				}

				if(err) return err;

				continue;
			}
		}

		err = av_read_frame(format_ctx, packet);

		if(err) return err;

		time = (double)packet -> pts / stream -> time_base.den;
		time -= time_start;

		bool uninit_pipeline = false;

		if(filters_set()){
			if(!pipeline && (err = init_pipeline()) < 0)
				return err;
		}else{
			if(pipeline && stream -> codecpar -> codec_id == PLAYER_OUTPUT_CODEC)
				uninit_pipeline = true;
		}

		if(!pipeline || uninit_pipeline){
			int sample_rate = 48000, /* opus is always 48KHz */
				channels = opus_packet_get_nb_channels(packet -> data),
				samples = opus_packet_get_samples_per_frame(packet -> data, sample_rate);
			if(samples == OPUS_INVALID_PACKET)
				return AVERROR(EINVAL);
			if(channels == audio_out.channels && sample_rate == audio_out.sample_rate)
				packet -> duration = samples;
			else{
				uninit_pipeline = false;

				if(!pipeline && (err = init_pipeline()) < 0)
					return err;
			}

			if(uninit_pipeline)
				pipeline_deinit();
			if(!pipeline)
				break;
		}

		err = avcodec_send_packet(decoderctx, packet);

		if(err) return err;

		decoder_has_data = true;
	}

	return 0;
}

void Player::run(){
	AVDictionary* options = nullptr;

	int err = AVERROR(ENOMEM);
	int stream_index;

	timespec sleep, now;

	if(!frame)
		frame = av_frame_alloc();
	if(!packet)
		packet = av_packet_alloc();
	if(!frame || !packet)
		goto end;
	format_ctx = avformat_alloc_context();

	if(!format_ctx)
		goto end;
	format_ctx -> protocol_whitelist = strdup("http,https,tcp,tls,crypto");
	format_ctx -> interrupt_callback.callback = decode_interrupt;
	format_ctx -> interrupt_callback.opaque = this;

	if(!format_ctx -> protocol_whitelist)
		goto end;
	if((err = av_dict_set(&options, "user_agent", "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/89.0.4389.72 Safari/537.36", AV_DICT_MATCH_CASE)) < 0)
		goto end;
	if((err = av_dict_set(&options, "scan_all_pmts", "1", AV_DICT_MATCH_CASE)) < 0)
		goto end;
	if((err = av_dict_set(&options, "reconnect", "1", AV_DICT_MATCH_CASE)) < 0)
		goto end;
	// av_dict_set(&options, "reconnect_at_eof", "1", AV_DICT_MATCH_CASE);
	if((err = av_dict_set(&options, "reconnect_on_network_error", "1", AV_DICT_MATCH_CASE)) < 0)
		goto end;
	// av_dict_set(&options, "reconnect_on_http_error", "1", AV_DICT_MATCH_CASE);
	if((err = av_dict_set(&options, "reconnect_delay_max", "2", AV_DICT_MATCH_CASE)) < 0)
		goto end;
	if((err = av_dict_set(&options, "icy", "0", AV_DICT_MATCH_CASE)) < 0)
		goto end;
	error.error.clear();
	err = avformat_open_input(&format_ctx, url.c_str(), nullptr, &options);

	if(err){
		switch(err){
			case AVERROR(EINVAL):
				error.error += "Invalid input file";

				break;
			case AVERROR(EIO):
				error.error += "Could not open input file";

				break;
			default:
				goto end;
		}

		error.code = err;

		send_message(PLAYER_ERROR);

		return;
	}

	for(int i = 0; i < format_ctx -> nb_streams; i++){
		AVStream* stream = format_ctx -> streams[i];

		if(stream -> codecpar -> codec_type != AVMEDIA_TYPE_AUDIO)
			stream -> discard = AVDISCARD_ALL;
	}

	if((err = avformat_find_stream_info(format_ctx, nullptr)) < 0)
		goto end;
	for(int i = 0; i < format_ctx -> nb_streams; i++){
		AVStream* stream = format_ctx -> streams[i];

		stream -> discard = AVDISCARD_ALL;
	}

	stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

	if(stream_index < 0){
		err = stream_index;

		goto end;
	}

	stream = format_ctx -> streams[stream_index];
	stream -> discard = AVDISCARD_DEFAULT;

	if(stream -> duration != AV_NOPTS_VALUE)
		duration = (double)stream -> duration / stream -> time_base.den;
	else
		duration = (double)format_ctx -> duration / AV_TIME_BASE;
	if(format_ctx -> start_time != AV_NOPTS_VALUE)
		time_start = (double)format_ctx -> start_time / AV_TIME_BASE;
	else
		time_start = 0;
	if(stream -> codecpar -> codec_id != PLAYER_OUTPUT_CODEC && (err = init_pipeline()) < 0)
		goto end;
	send_message(PLAYER_READY);
	clock_gettime(CLOCK_MONOTONIC, &sleep);

	audio_in.reset();
	decoder_has_data = false;
	encoder_has_data = false;
	filter_has_data = false;

	while(!b_stop){
		if(b_bitrate){
			b_bitrate = false;

			if(pipeline){
				avcodec_close(encoderctx);

				encoderctx -> bit_rate = bitrate;

				if((err = avcodec_open2(encoderctx, encoder, nullptr)) < 0)
					goto end;
			}
		}

		if(b_seek){
			b_seek = false;

			if(seek_to < 0)
				seek_to = 0;
			else if(seek_to > duration)
				seek_to = duration;
			seek_to += time_start;

			int64_t time = (int64_t)(seek_to * stream -> time_base.den);

			err = avformat_seek_file(format_ctx, stream_index, time - 1, time, time + 1, 0);

			if(!err){
				if(pipeline)
					avcodec_flush_buffers(decoderctx);
				if(filter_graph && (err = configure_filters()) < 0)
					goto end;
			}
		}

		if(pause){
			wait_cond([&]{
				return pause;
			});

			clock_gettime(CLOCK_MONOTONIC, &sleep);
		}

		err = read_packet();

		if(b_stop)
			break;
		if(err < 0){
			if(err == AVERROR_EOF){
				send_message(PLAYER_FINISH);

				wait_cond([&](){
					return !destroyed && !b_seek;
				});

				if(destroyed)
					break;
				continue;
			}

			if(err == AVERROR_EXIT)
				break;
			goto end;
		}

		if(b_stop)
			break;
		int64_t dur = packet -> duration;
		int den = pipeline ? encoderctx -> time_base.den : audio_out.sample_rate;

		sleep.tv_nsec += dur * 1'000'000'000 / den;

		if(sleep.tv_nsec > 1'000'000'000){
			sleep.tv_sec += sleep.tv_nsec / 1'000'000'000;
			sleep.tv_nsec %= 1'000'000'000;
		}

		clock_gettime(CLOCK_MONOTONIC, &now);

		if(now.tv_sec > sleep.tv_sec || (now.tv_sec == sleep.tv_sec && now.tv_nsec > sleep.tv_nsec)){
			unsigned long time = (now.tv_sec - sleep.tv_sec) * 1'000'000'000 + now.tv_nsec - sleep.tv_nsec;

			dropped_frames += time * den / (dur * 1'000'000'000);
			sleep = now;
		}else{
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sleep, nullptr);
		}

		total_frames++;

		send_message(PLAYER_PACKET);
		av_packet_unref(packet);
	}

	return;

	end:

	if(err)
		send_error(err);
	return;
}


void Player::cleanup(){
	avformat_close_input(&format_ctx);
	avfilter_graph_free(&filter_graph);
	avcodec_free_context(&encoderctx);
	avcodec_free_context(&decoderctx);
}

void Player::player_thread(){
	while(!destroyed){
		if(b_stop && !b_start){
			wait_cond([&]{
				return b_stop && !b_start;
			});
		}

		run();
		cleanup();

		b_stop = !b_start;
		b_start = false;
	}

	running = false;

	delete this;

	std::cout << "exit thread" << std::endl;
}

void Player::signal_cond(){
	uv_mutex_lock(&mutex);
	uv_cond_signal(&cond);
	uv_mutex_unlock(&mutex);
}

template<class T>
void Player::wait_cond(T t){
	uv_mutex_lock(&mutex);

	while(t())
		uv_cond_wait(&cond, &mutex);
	uv_mutex_unlock(&mutex);
}

void Player::send_message(PlayerSignal sig){
	signal = sig;
	queued = true;
	waiting = true;
	message.send();

	wait_cond([&](){
		return queued;
	});

	waiting = false;
}

void Player::received_message(){
	wrapper -> signal(signal);
	queued = false;

	if(waiting)
		signal_cond();
}

void Player::send_error(int err){
	char errbuf[128];

	av_strerror(err, errbuf, sizeof(errbuf));

	error.error = errbuf;
	error.code = err;

	send_message(PLAYER_ERROR);
}

Player::Player(PlayerWrapper* w) : message(this){
	wrapper = w;

	time = 0;
	time_start = 0;
	duration = 0;
	dropped_frames = 0;
	total_frames = 0;
	total_packets = 0;
	destroyed = false;
	running = false;
	queued = false;

	pipeline = false;

	b_stop = false;
	b_start = false;
	pause = false;
	b_seek = false;
	b_bitrate = false;
	bitrate = 0;
	seek_to = 0;

	format_ctx = nullptr;
	filter_graph = nullptr;
	filter_src = nullptr;
	filter_sink = nullptr;
	stream = nullptr;

	encoder = avcodec_find_encoder(AV_CODEC_ID_OPUS);

	decoderctx = nullptr;
	encoderctx = nullptr;

	frame = nullptr;
	packet = nullptr;

	audio_out.reset();

	error.error.reserve(256);

	uv_cond_init(&cond);
	uv_mutex_init(&mutex);
}

void Player::start(){
	if(running){
		b_start = true;

		signal_cond();

		return;
	}

	int err;

	err = message.async_init();

	if(err)
		throw PlayerException("Could not create thread communicator", err);
	err = uv_thread_create(&thread, s_player_thread, this);

	if(err)
		throw PlayerException("Could not create thread", err);
	running = true;
}

void Player::setURL(std::string _url){
	url = _url;
}

void Player::setFormat(int channels, int sample_rate, int brate){
	audio_out.channels = channels;
	audio_out.sample_rate = sample_rate;
	bitrate = brate;
}

double Player::getTime(){
	if(b_seek)
		return seek_to;
	return time;
}

double Player::getDuration(){
	return duration;
}

long Player::getDroppedFrames(){
	return dropped_frames;
}

long Player::getTotalFrames(){
	return total_frames;
}

long Player::getTotalPackets(){
	return total_packets;
}

void Player::setPaused(bool paused){
	pause = paused;

	if(!paused)
		signal_cond();
}

void Player::seek(double time){
	b_seek = true;
	seek_to = time;

	signal_cond();
}

void Player::setBitrate(int bt){
	b_bitrate = true;
	bitrate = bt;
}

void Player::setVolume(float v){
	uv_mutex_lock(&mutex);

	volume.set(v);

	uv_mutex_unlock(&mutex);
}

void Player::setRate(float r){
	uv_mutex_lock(&mutex);

	rate.set(r);

	uv_mutex_unlock(&mutex);
}

void Player::setTempo(float t){
	uv_mutex_lock(&mutex);

	tempo.set(t);

	uv_mutex_unlock(&mutex);
}

void Player::setTremolo(float depth, float rate){
	uv_mutex_lock(&mutex);

	tremolo.set(depth, rate);

	uv_mutex_unlock(&mutex);
}

void Player::setEqualizer(Equalizer* eqs, size_t length){
	uv_mutex_lock(&mutex);

	equalizer.set(eqs, length);

	uv_mutex_unlock(&mutex);
}

void Player::stop(){
	b_stop = true;
}

void Player::destroy(){
	destroyed = true;

	if(running)
		signal_cond();
	else
		delete this;
}

const PlayerError& Player::getError(){
	return error;
}

Player::~Player(){
	cleanup();
	av_packet_free(&packet);
	av_frame_free(&frame);
}