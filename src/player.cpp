#include <time.h>
#include <unistd.h>
#include <opus/opus.h>
#include "player.h"

void PlayerContext::add(Player* player){
	mutex.lock();

	if(list)
		list -> prev = player;
	player -> next = list;
	player -> prev = nullptr;
	list = player;

	mutex.unlock();
}

void PlayerContext::remove(Player* player){
	mutex.lock();

	if(player == list || player -> prev){
		player -> thread.detach();

		if(player == list){
			list = player -> next;

			if(list)
				list -> prev = nullptr;
		}else{
			player -> prev -> next = player -> next;

			if(player -> next)
				player -> next -> prev = player -> prev;
		}
	}

	mutex.unlock();
}

void PlayerContext::wait_threads(){
	Player* player;
	Thread thread;

	while(true){
		player = nullptr;
		mutex.lock();
		player = list;

		if(player){
			list = player -> next;
			player -> prev = nullptr;
			player -> next = nullptr;
			thread = player -> thread;
		}

		mutex.unlock();

		if(!player)
			break;
		thread.join();
	}
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

void Player::pipeline_destroy(){
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

		if(!outputs -> name || !inputs -> name){
			ret = AVERROR(ENOMEM);

			goto failgraph;
		}

		mutex.lock();

		ret = 0;

		try{
			std::string filter("");

			if(rate.is_set())
				filter += "," + rate.to_string(audio_in, audio_out);
			if(tempo.is_set())
				filter += "," + tempo.to_string(audio_in, audio_out);
			if(tremolo.is_set())
				filter += "," + tremolo.to_string(audio_in, audio_out);
			if(volume.is_set())
				filter += "," + volume.to_string(audio_in, audio_out);
			if(equalizer.is_set())
				filter += "," + equalizer.to_string(audio_in, audio_out);
			ret = avfilter_graph_parse_ptr(filter_graph, filter.c_str() + 1, &inputs, &outputs, nullptr);
		}catch(std::bad_alloc& e){
			ret = AVERROR(ENOMEM);
		}

		mutex.unlock();

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

				av_frame_unref(frame);

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

					if(!err)
						av_frame_unref(frame);
				}

				if(err){
					av_frame_unref(frame);

					return err;
				}

				continue;
			}
		}

		err = av_read_frame(format_ctx, packet);

		if(err) return err;

		time = (double)packet -> pts / stream -> time_base.den;
		time -= time_start;

		bool destroy_pipeline = false;

		if(filters_set()){
			if(!pipeline && (err = init_pipeline()) < 0)
				return err;
		}else{
			if(pipeline && stream -> codecpar -> codec_id == encoder_id)
				destroy_pipeline = true;
		}

		if(!pipeline || destroy_pipeline){
			if(encoder_id == AV_CODEC_ID_OPUS){
				int sample_rate = 48000, /* opus is always 48KHz */
					channels = opus_packet_get_nb_channels(packet -> data),
					samples = opus_packet_get_samples_per_frame(packet -> data, sample_rate);
				if(samples == OPUS_INVALID_PACKET)
					return AVERROR_INVALIDDATA;
				if(channels == audio_out.channels && sample_rate == audio_out.sample_rate)
					packet -> duration = samples;
				else{
					destroy_pipeline = false;

					if(!pipeline && (err = init_pipeline()) < 0)
						return err;
				}
			}

			if(destroy_pipeline)
				pipeline_destroy();
			if(!pipeline)
				break;
		}

		err = avcodec_send_packet(decoderctx, packet);

		av_packet_unref(packet);

		if(err) return err;

		decoder_has_data = true;
	}

	return 0;
}

void Player::run(){
	AVDictionary* options = nullptr;
	std::string local_url;

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
	if((err = av_dict_set(&options, "reconnect_on_network_error", "1", AV_DICT_MATCH_CASE)) < 0)
		goto end;
	if((err = av_dict_set(&options, "reconnect_delay_max", "2", AV_DICT_MATCH_CASE)) < 0)
		goto end;
	if((err = av_dict_set(&options, "icy", "0", AV_DICT_MATCH_CASE)) < 0)
		goto end;
	error.str.clear();

	mutex.lock();
	local_url = std::move(url);
	mutex.unlock();

	err = avformat_open_input(&format_ctx, local_url.c_str(), nullptr, &options);

	mutex.lock();

	if(url.empty())
		url = std::move(local_url);
	mutex.unlock();

	av_dict_free(&options);

	if(err){
		switch(err){
			case AVERROR(EINVAL):
				error.str += "Invalid input file";

				break;
			case AVERROR(EIO):
				error.str += "Could not open input file";

				break;
			default:
				goto end;
		}

		error.code = err;

		goto err;
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
	if(stream -> codecpar -> codec_id != encoder_id && (err = init_pipeline()) < 0)
		goto end;
	audio_in.reset();
	decoder_has_data = false;
	encoder_has_data = false;
	filter_has_data = false;

	err = callback_wrap([&]{
		return callbacks -> ready(this);
	});

	if(err)
		return;
	clock_gettime(CLOCK_MONOTONIC, &sleep);

	while(should_run()){
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
			int64_t time;

			time = (int64_t)((seek_to + time_start) * stream -> time_base.den);
			err = avformat_seek_file(format_ctx, stream_index, time - 1, time, time + 1, 0);
			b_seek = false;

			if(!should_run())
				break;
			if(!err){
				err = callback_wrap([&]{
					return callbacks -> seeked(this);
				});

				if(err)
					break;
				if(pipeline)
					avcodec_flush_buffers(decoderctx);
				if(filter_graph && (err = configure_filters()) < 0)
					goto end;
			}
		}

		if(b_pause){
			wait_cond([&]{
				return b_pause && should_run();
			});

			if(!should_run())
				break;
			clock_gettime(CLOCK_MONOTONIC, &sleep);
		}

		err = read_packet();

		if(!should_run())
			break;
		if(err < 0){
			if(err == AVERROR_EOF){
				err = callback_wrap([&]{
					return callbacks -> finish(this);
				});

				if(err)
					break;
				wait_cond([&](){
					return should_run() && !b_seek;
				});

				if(!should_run())
					break;
				continue;
			}

			if(err == AVERROR_EXIT)
				break;
			goto end;
		}

		long dur = packet -> duration,
			den = pipeline ? encoderctx -> time_base.den : audio_out.sample_rate;
		if(dur < 0)
			dur = 0; /* should never happen but just in case */
		if(den <= 0){
			error.str += "Fatal error: den <= 0";
			err = AVERROR_EXIT;

			goto err;
		}

		err = callback_wrap([&]{
			return callbacks -> packet(this, packet);
		});

		av_packet_unref(packet);

		if(err)
			break;
		sleep.tv_nsec += dur * 1'000'000'000 / den;

		if(sleep.tv_nsec > 1'000'000'000){
			sleep.tv_sec += sleep.tv_nsec / 1'000'000'000;
			sleep.tv_nsec %= 1'000'000'000;
		}

		clock_gettime(CLOCK_MONOTONIC, &now);

		if(now.tv_sec > sleep.tv_sec || (now.tv_sec == sleep.tv_sec && now.tv_nsec > sleep.tv_nsec)){
			unsigned long time = (now.tv_sec - sleep.tv_sec) * 1'000'000'000 + now.tv_nsec - sleep.tv_nsec;

			dropped_samples += time * den / 1'000'000'000;
			sleep = now;
		}else{
			mutex.lock();
			cond.wait(mutex, sleep);
			mutex.unlock();

			if(!should_run())
				break;
		}

		total_samples += dur;
		total_packets++;
		err = callback_wrap([&]{
			return callbacks -> send_packet(this);
		});

		if(err == AVERROR(EAGAIN))
			dropped_samples += dur;
		else if(err)
			break;
	}

	return;

	end:

	if(!err)
		return;
	else{
		char errbuf[128];

		av_strerror(err, errbuf, sizeof(errbuf));

		error.str += errbuf;
		error.code = err;
	}

	err:

	callback_wrap([&]{
		callbacks -> error(this, error.str, error.code);

		return 0;
	}, false);
}

template<class T>
int Player::callback_wrap(T t, bool run){
	if((run && !should_run()) || destroyed)
		return AVERROR_EXIT;
	else
		return t();
}

void Player::cleanup(){
	avformat_close_input(&format_ctx);
	pipeline_destroy();

	if(packet)
		av_packet_unref(packet);
}

void Player::player_thread(){
	while(!destroyed){
		if(b_stop && !b_start){
			wait_cond([&]{
				return !destroyed && b_stop && !b_start;
			});
		}

		if(destroyed)
			break;
		run();
		cleanup();

		b_stop = !b_start;
		b_start = false;
	}

	mutex.lock();
	running = false;
	mutex.unlock();
	context -> remove(this);

	delete this;
}

bool Player::should_run(){
	return !destroyed && !b_stop;
}

void Player::signal_cond(){
	mutex.lock();
	cond.signal();
	mutex.unlock();
}

template<class T>
void Player::wait_cond(T t){
	mutex.lock();

	while(t())
		cond.wait(mutex);
	mutex.unlock();
}

Player::Player(PlayerContext* ctx, PlayerCallbacks* c, void* d): cond(CLOCK_MONOTONIC), thread(s_player_thread, this){
	context = ctx;
	next = nullptr;
	prev = nullptr;

	callbacks = c;
	data = d;

	time = 0;
	time_start = 0;
	duration = 0;
	dropped_samples = 0;
	total_samples = 0;
	total_packets = 0;
	destroyed = false;
	running = false;

	pipeline = false;

	b_stop = false;
	b_start = false;
	b_pause = false;
	b_seek = false;
	b_bitrate = false;
	bitrate = 0;
	seek_to = 0;

	format_ctx = nullptr;
	filter_graph = nullptr;
	filter_src = nullptr;
	filter_sink = nullptr;
	stream = nullptr;

	decoderctx = nullptr;
	encoderctx = nullptr;

	frame = nullptr;
	packet = nullptr;

	audio_out.reset();

	error.str.reserve(256);
}

int Player::start(){
	if(running){
		b_start = true;

		signal_cond();

		return 0;
	}

	int err = thread.start();

	if(err)
		return err;
	context -> add(this);
	running = true;

	return 0;
}

void Player::setURL(std::string _url){
	mutex.lock();
	url = _url;
	mutex.unlock();
}

void Player::setOutputCodec(AVCodecID id){
	encoder_id = id;
	encoder = avcodec_find_encoder(encoder_id);
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

long Player::getDroppedSamples(){
	return dropped_samples;
}

long Player::getTotalSamples(){
	return total_samples;
}

long Player::getTotalPackets(){
	return total_packets;
}

void Player::setPaused(bool paused){
	b_pause = paused;

	if(!paused)
		signal_cond();
}

void Player::seek(double time){
	seek_to = time;

	if(seek_to < 0)
		seek_to = 0;
	else if(seek_to > duration)
		seek_to = duration;
	b_seek = true;

	signal_cond();
}

void Player::setBitrate(int bt){
	bitrate = bt;
	b_bitrate = true;
}

void Player::setVolume(float v){
	mutex.lock();
	volume.set(v);
	mutex.unlock();
}

void Player::setRate(float r){
	mutex.lock();
	rate.set(r);
	mutex.unlock();
}

void Player::setTempo(float t){
	mutex.lock();
	tempo.set(t);
	mutex.unlock();
}

void Player::setTremolo(float depth, float rate){
	mutex.lock();
	tremolo.set(depth, rate);
	mutex.unlock();
}

void Player::setEqualizer(Equalizer* eqs, size_t length){
	mutex.lock();

	try{
		equalizer.set(eqs, length);
	}catch(...){
		mutex.unlock();

		throw;
	}

	mutex.unlock();
}

void Player::stop(){
	b_stop = true;
}

void Player::destroy(){
	bool free = false;

	mutex.lock();

	if(!destroyed){
		destroyed = true;

		if(running)
			cond.signal();
		else
			free = true;
	}

	mutex.unlock();

	if(free)
		delete this;
}

bool Player::isCodecCopy(){
	return !pipeline;
}

Player::~Player(){
	cleanup();
	av_packet_free(&packet);
	av_frame_free(&frame);
}