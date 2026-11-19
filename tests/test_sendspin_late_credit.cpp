// SPDX-License-Identifier: MIT
// A real C99 sink services DATA only after the SDK's write budget expires.
#include "engine_sink.h"
#include "sendspin_engine_fixture.h"
#include <chrono>
#include <thread>
#include <vector>
using namespace libreecho::sendspin;
using namespace libreecho::sendspin::test;
static void scenario(bool feedback_between,bool finish_pending) {
    EngineServer server;
    const auto path=unique_socket_path("late");
    CHECK(server.start(path),"real sink starts");
    EngineSink sink;
    CHECK(sink.connect(path,1000)==SinkResult::Ok && sink.open(1,1000)==SinkResult::Ok,"OPEN");
    std::vector<int16_t> pcm(512*2,456);
    const auto* bytes=reinterpret_cast<const uint8_t*>(pcm.data());
    server.pause();
    CHECK(sink.on_audio_write(bytes,pcm.size()*2,5)==0,"uncredited DATA returns zero by deadline");
    CHECK(sink.accepted_frames()==0,"uncredited DATA never advances client cursor");
    server.resume();
    if(feedback_between) {
        EngineSinkTiming timing{};
        CHECK(sink.progress(&timing,1000)==SinkResult::Ok,"feedback preserves rather than discards late CREDIT");
    }
    if(finish_pending) {
        CHECK(sink.finish(0,1000,nullptr)==SinkResult::Ok,"FINISH resolves outstanding credited DATA before exact total");
    } else {
        CHECK(sink.on_audio_write(bytes,pcm.size()*2,1000)==pcm.size()*2,"identical SDK retry receives late CREDIT without duplicate DATA");
        CHECK(sink.on_audio_write(bytes,pcm.size()*2,1000)==pcm.size()*2,"next sequence still accepts after delayed credit");
    }
    uint64_t accepted=0;
    server.with_sink([&](le_audio_sink* s){le_audio_sink_progress p{};
        CHECK(le_audio_sink_get_progress(s,&p)==LE_AUDIO_SINK_OK,"engine progress"); accepted=p.accepted_frames;});
    CHECK(accepted==(finish_pending?512:1024),"engine received each source block exactly once");
    CHECK(sink.accepted_frames()==accepted,"engine and acknowledged source cursors agree");
    sink.disconnect(); server.stop();
}
static void cached_progress_survives_writer() {
    EngineServer server; auto path=unique_socket_path("latep"); CHECK(server.start(path),"progress sink");
    EngineSink sink; CHECK(sink.connect(path,1000)==SinkResult::Ok && sink.open(1,1000)==SinkResult::Ok,"progress OPEN");
    EngineSinkTiming timing{};
    server.pause(); CHECK(sink.progress(&timing,5)==SinkResult::Timeout,"progress misses its original deadline");
    server.resume(); std::vector<int16_t> pcm(512*2,123);
    CHECK(sink.on_audio_write(reinterpret_cast<const uint8_t*>(pcm.data()),pcm.size()*2,1000)==pcm.size()*2,"writer receives late progress before CREDIT");
    server.pause();
    CHECK(sink.progress(&timing,5)==SinkResult::Ok,"late PROGRESS retained by writer is returned without another request");
    sink.disconnect();server.stop();
}
// A slow engine may outlive several main-loop deadlines. Retrying the one
// outstanding request must not queue a new PROGRESS reply on every call.
static void timed_out_progress_is_one_inflight_request() {
    EngineServer server; const auto path=unique_socket_path("latep");
    CHECK(server.start(path),"progress retry sink");
    EngineSink sink;
    CHECK(sink.connect(path,1000)==SinkResult::Ok && sink.open(1,1000)==SinkResult::Ok,
          "progress retry OPEN");
    EngineSinkTiming timing{};
    server.pause();
    for(int i=0;i<12;++i)
        CHECK(sink.progress(&timing,5)==SinkResult::Timeout,"progress retry stays bounded");
    server.resume();
    CHECK(sink.progress(&timing,1000)==SinkResult::Ok,"adopt the original delayed progress");
    server.pause();
    CHECK(sink.progress(&timing,5)==SinkResult::Timeout,
          "one reply exhausted the old request; no duplicate replies remain queued");
    sink.disconnect();server.stop();
}
int main(){scenario(false,false);scenario(true,false);scenario(true,true);cached_progress_survives_writer();
    timed_out_progress_is_one_inflight_request();
    if(g_failures) return 1;
    std::puts("test_sendspin_late_credit: all checks passed");return 0;
}
