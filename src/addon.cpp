#include <napi.h>
#include "wrapper.h"
#include "ffmpeg.h"

Napi::Object init(Napi::Env env, Napi::Object exports){
	avformat_network_init();

#ifdef SANGE_DEBUG
	av_log_set_level(AV_LOG_TRACE);
#else
	av_log_set_level(AV_LOG_FATAL);
#endif
	exports = PlayerWrapper::init(env);

	return exports;
}

NODE_API_MODULE(addon, init)