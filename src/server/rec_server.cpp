#include <brpc/server.h>
#include <nlohmann/json.hpp>

#include "rec.pb.h"
#include "rec_server.h"
#include "common/config.h"
#include "common/log.h"
#include "data/exp.h"
#include "data/item_feature.h"
#include "data/item_pool.h"
#include "fetcher/fetcher.h"
#include "rec/context.h"
#include "rec/recommend.h"

namespace ratus_rec {

namespace rec_server {

using json = nlohmann::json;

namespace {

static thread_local Context ctx;

class RecommendServiceImpl : public ratus_rec::RecommendService {
public:
    void recommend(google::protobuf::RpcController* cntl,
                    const RecRequest* req,
                    RecResponse* res,
                    google::protobuf::Closure* done) override {

        brpc::ClosureGuard guard(done);

        ctx.reset();

        ctx.user_id             = req->user_id();
        ctx.group_id            = get_user_group(ctx.user_id);
        ctx.timestamp           = req->timestamp();
        ctx.request_id          = req->request_id();
        ctx.session_id          = req->session_id();
        ctx.session_refresh_num = req->session_refresh_num();
        ctx.topk                = req->topk();

        for (const auto& item : req->last_refresh_items()) {
            LastRefreshItem new_item;
            new_item.set_item_id(item.item_id());
            new_item.set_stay_duration(item.stay_duration());
            ctx.last_refresh_items.push_back(new_item);
        }

        ctx.item_pool       = std::atomic_load(&g_item_pool);
        ctx.item_feature    = std::atomic_load(&g_item_feature);
        ctx.exp_config      = std::atomic_load(&g_exp_merged_config);

        fetcher::fetch_user_context(ctx);

        json result = rec::recommend(ctx);
    
        res->set_code(result["code"]);
        res->set_message(result["message"]);
        res->set_request_id(result["request_id"]);
        res->set_timestamp(result["timestamp"]);

        auto data = res->mutable_data();
        data->set_current_refresh_num(result["data"]["current_refresh_num"]);

        for (const auto& feed : result["data"]["feed_list"]) {
            auto new_feed = data->add_feed_list();
            new_feed->set_item_id(feed["item_id"]);
            new_feed->set_score(feed["score"]);
        }

        auto ext = data->mutable_ext_info();
        ext->set_has_more(result["data"]["ext_info"]["has_more"]);
        ext->set_strategy(result["data"]["ext_info"]["strategy"]);
    }
};

} // namespace

bool init() {
    if (!fetcher::init()) {
        ALOG(ERROR, "fetcher init failed");
        return false;
    }

    ALOG(INFO, "server init success");
    return true;
}

void start() {
    brpc::Server server;
    brpc::ServerOptions options;
    options.num_threads = g_config.server.worker_num;

    server.AddService(new RecommendServiceImpl, brpc::SERVER_OWNS_SERVICE);

    if (server.Start(g_config.server.port, &options) != 0) {
        ALOG(FATAL, "server start failed port=%d", g_config.server.port);
        return;
    }

    ALOG(INFO, "rec_server started port=%d worker=%d", g_config.server.port, g_config.server.worker_num);

    server.RunUntilAskedToQuit();
}

} // namespace rec_server

} // namespace ratus_rec
