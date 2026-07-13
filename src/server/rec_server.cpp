#include <brpc/server.h>
#include <nlohmann/json.hpp>

#include "recommend.pb.h"
#include "rec_server.h"
#include "common/config.h"
#include "common/log.h"
#include "data/exp.h"
#include "data/item_feature.h"
#include "data/item_pool.h"
#include "fetcher/fetcher.h"
#include "rec/context.h"
#include "rec/recommend.h"

namespace predictionmarkets_rec {

namespace rec_server {

using json = nlohmann::json;

namespace {

class RecommendServiceImpl : public predictionmarkets_rec::PredictionMarketsRecommend {
public:
    void recommend(google::protobuf::RpcController* cntl,
                    const RecRequest* req,
                    RecResponse* res,
                    google::protobuf::Closure* done) override {

        brpc::ClosureGuard guard(done);

        try {
            Context ctx;

            ctx.user_id             = req->user_id();
            ctx.device_id           = req->device_id();
            ctx.anchor_id           = !ctx.user_id.empty() ? ctx.user_id : ctx.device_id;

            // user_id 与 device_id 均空则无从锚定，直接拒绝
            if (ctx.anchor_id.empty()) {
                ALOG(WARNING, "reject request without user_id/device_id, request_id: %s", req->request_id().c_str());
                res->set_code(400);
                res->set_message("missing user_id and device_id");
                res->set_request_id(req->request_id());
                res->set_timestamp(req->timestamp());
                return;
            }

            ctx.group_id            = get_user_group(ctx.anchor_id);
            ctx.timestamp           = req->timestamp();
            ctx.request_id          = req->request_id();
            ctx.session_id          = req->session_id();
            ctx.session_refresh_num = req->session_refresh_num();
            ctx.topk                = req->topk();

            // 付费用户取全量池，免费用户取免费池；下游只认 ctx.item_pool，不再判断付费
            ctx.item_pool       = req->is_paid_user() ? std::atomic_load(&g_all_pool)
                                                      : std::atomic_load(&g_free_pool);
            ctx.item_feature    = std::atomic_load(&g_item_feature);
            ctx.exp_config      = std::atomic_load(&g_exp_merged_config);

            // 用户上下文：当前仅拉推荐历史（rec_history）做去重软降权；
            // 黑名单/曝光历史/关注暂在 fetch_user_context 内注释，恢复时一并放开
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
                new_feed->set_callback_feature(feed["callback_feature"]);
            }

            auto ext = data->mutable_ext_info();
            ext->set_strategy(result["data"]["ext_info"]["strategy"]);
        } catch (const std::exception& e) {
            ALOG(ERROR, "recommend exception, request_id: %s, error: %s", req->request_id().c_str(), e.what());
            res->set_code(500);
            res->set_message("internal error");
            res->set_request_id(req->request_id());
            res->set_timestamp(req->timestamp());
        }
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
    // num_threads=0 → 跳过 brpc Start 里的 setconcurrency。ServerOptions 默认 num_threads=
    // #core+1（低核机器如 4 核=5）可能小于 bthread 全局默认并发（init 期间 brpc 后台 bthread
    // 已把并发抬到 9），Start 想设回更小值会触发 "concurrency should be larger than old"
    // warning 且不生效。设 0 跳过该调用、沿用现有并发；如需调整用 gflag -bthread_concurrency（只增不减）。
    options.num_threads = 0;

    server.AddService(new RecommendServiceImpl, brpc::SERVER_OWNS_SERVICE);

    if (server.Start(g_config.server.port, &options) != 0) {
        ALOG(FATAL, "server start failed port=%d", g_config.server.port);
        return;
    }

    ALOG(INFO, "rec_server started port=%d", g_config.server.port);

    server.RunUntilAskedToQuit();
}

} // namespace rec_server

} // namespace predictionmarkets_rec
