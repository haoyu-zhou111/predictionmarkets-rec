#include "recall.h"
#include "filter.h"
#include "common/log.h"
#include "common/config.h"
#include "common/redis.h"
#include "data/i2i_index.h"

namespace predictionmarkets_rec {

namespace rec {

namespace {

bool add_candidate(Context& ctx, const ItemId& item, const std::string& channel) {
    if (!check_item(ctx, item)) {
        return false;
    }

    auto channel_iter = CHANNEL.find(channel);
    if (channel_iter == CHANNEL.end()) {
        ALOG(DEBUG, "channel %s not exists", channel.c_str());
        return false;
    }

    auto channel_id = channel_iter->second;
    if (ctx.channels.find(item) == std::end(ctx.channels)) {
        ctx.candidates.emplace_back(item);
    }
    ctx.channels[item] |= (1 << channel_id);
    return true;
}

template <typename Trigger, typename IndexMap>
void trigger_index_recall(Context& ctx, const std::string& channel, const RecallChannelConfig& conf, const std::vector<Trigger>& triggers, const IndexMap& index_map) {
    uint32_t tot = 0;
    for (auto& trigger : triggers) {
        const auto item_vec_iter = index_map.find(trigger);
        if (item_vec_iter == index_map.end()) {
            continue;
        }
        uint32_t single = 0;
        for (auto& item : item_vec_iter->second) {
            if (add_candidate(ctx, item, channel)) {
                single++;
                tot++;
                if (single == conf.single_topk || tot == conf.topk) {
                    break;
                }
            }
        }
        if (tot == conf.topk) {
            break;
        }
    }
}   

void i2i_recall(Context& ctx, const std::string& channel, const RecallChannelConfig& conf) {
    auto index_iter = g_i2i_index_map.find(channel);
    if (index_iter == g_i2i_index_map.end()) {
        ALOG(ERROR, "channel %s i2i index not exists", channel.c_str());
        return;
    }

    const auto& index = index_iter->second;
    trigger_index_recall(ctx, channel, conf, ctx.click_list, index);
}

// cate_hot 通道暂停：需要 item_pool 提供「按分区的热门倒排索引」，本期未建（见 TODO），
// 后续补上分区索引后恢复。原逻辑保留如下：
// void cate_hot_recall(Context& ctx, const std::string& channel, const RecallChannelConfig& conf) {
//     trigger_index_recall(ctx, channel, conf, ctx.recent_cates_vec, /* item_pool 分区热门索引 */);
// }

void follow_u2i_recall(Context& ctx, const std::string& channel, const RecallChannelConfig& conf) {
    trigger_index_recall(ctx, channel, conf, ctx.follow_u2i_triggers, ctx.follow_u2i_items_map);
}

void recent_u2i_recall(Context& ctx, const std::string& channel, const RecallChannelConfig& conf) {
    trigger_index_recall(ctx, channel, conf, ctx.recent_u2i_triggers, ctx.recent_u2i_items_map);
}

void global_hot_recall(Context& ctx, const std::string& channel, const RecallChannelConfig& conf) {
    uint32_t tot = 0;
    for (const auto& item : ctx.item_pool->hot_items) {
        if (add_candidate(ctx, item, channel)) {
            tot++;
            if (tot == conf.topk) {
                break;
            }
        }
    }
}

void op_recall(Context& ctx) {
    auto& op_config = ctx.exp_config->groups[ctx.group_id].op;
    if (!op_config.enable) {
        return;
    }
    for (const auto& item : op_config.items) {
        add_candidate(ctx, item, "op");
    }
}

void follow_u2i_prepare(
    Context& ctx,
    const std::string& channel,
    const RecallChannelConfig& conf,
    std::vector<std::string>& redis_cmds,
    std::vector<std::string>& redis_keys,
    std::unordered_map<std::string, size_t>& idx_mapping,
    std::vector<size_t>& parse_idx
) {

    ctx.follow_u2i_triggers = sample(ctx.followed_list, conf.trigger_count);
    for (auto& t : ctx.follow_u2i_triggers) {
        
        std::string c = "lrange" + g_config.user_context.published_redis_key;
        c += t;

        auto idx_iter = idx_mapping.find(c);
        if (idx_iter == idx_mapping.end()) {
            redis_cmds.emplace_back("lrange");
            redis_keys.emplace_back(g_config.user_context.published_redis_key + t);
            int idx = redis_cmds.size() - 1;
            idx_mapping[c] = idx;
            parse_idx.emplace_back(idx);
        } else {
            parse_idx.emplace_back(idx_iter->second);
        }
    }
}

void recent_u2i_prepare(
    Context& ctx,
    const std::string& channel,
    const RecallChannelConfig& conf,
    std::vector<std::string>& redis_cmds,
    std::vector<std::string>& redis_keys,
    std::unordered_map<std::string, size_t>& idx_mapping,
    std::vector<size_t>& parse_idx
) {

    for (size_t i = 0; i < std::min(ctx.recent_authors_vec.size(), static_cast<size_t>(conf.trigger_count)); i++) {
        auto& t = ctx.recent_authors_vec[i];
        ctx.recent_u2i_triggers.emplace_back(t);
        
        std::string c = "lrange" + g_config.user_context.published_redis_key;
        c += t;

        auto idx_iter = idx_mapping.find(c);
        if (idx_iter == idx_mapping.end()) {
            redis_cmds.emplace_back("lrange");
            redis_keys.emplace_back(g_config.user_context.published_redis_key + t);
            int idx = redis_cmds.size() - 1;
            idx_mapping[c] = idx;
            parse_idx.emplace_back(idx);
        } else {
            parse_idx.emplace_back(idx_iter->second);
        }
    }
}

void follow_u2i_parse(Context& ctx, const std::string& channel, brpc::RedisResponse& resp, const std::vector<size_t>& parse_idx) {
    for (size_t i = 0; i < ctx.follow_u2i_triggers.size(); i++) {
        auto& t = ctx.follow_u2i_triggers[i];

        try {
            ctx.follow_u2i_items_map[t] = redis::parse<std::vector<std::string>>(resp.reply(parse_idx[i]));
        } catch (const std::exception& e) {
            ALOG(ERROR, "parse user %s published item list failed: %s", t.c_str(), e.what());
        }
    }
}

void recent_u2i_parse(Context& ctx, const std::string& channel, brpc::RedisResponse& resp, const std::vector<size_t>& parse_idx) {
    for (size_t i = 0; i < ctx.recent_u2i_triggers.size(); i++) {
        auto& t = ctx.recent_u2i_triggers[i];

        try {
            ctx.recent_u2i_items_map[t] = redis::parse<std::vector<std::string>>(resp.reply(parse_idx[i]));
        } catch (const std::exception& e) {
            ALOG(ERROR, "parse user %s published item list failed: %s", t.c_str(), e.what());
        }
    }
}

using PrepareFunc = void (*)(
    Context& ctx,
    const std::string& channel,
    const RecallChannelConfig& conf,
    std::vector<std::string>& redis_cmds,
    std::vector<std::string>& redis_keys,
    std::unordered_map<std::string, size_t>& idx_mapping,
    std::vector<size_t>& parse_idx
);

using ParseFunc = void (*)(
    Context& ctx,
    const std::string& channel,
    brpc::RedisResponse& resp,
    const std::vector<size_t>& parse_idx
);

using RecallFunc = void (*)(
    Context& ctx,
    const std::string& channel,
    const RecallChannelConfig& conf
);

struct RecallChannel {
    std::string     name;
    RecallFunc      recall;
    PrepareFunc     prepare = nullptr;
    ParseFunc       parse   = nullptr;
};

const std::vector<RecallChannel> recalls = {
    {"icf",         i2i_recall},
    {"i2v",         i2i_recall},
    {"swing",       i2i_recall},
    {"global_hot",  global_hot_recall},
    // {"cate_hot",    cate_hot_recall},   // 暂停：缺分区热门索引，见上方说明与 TODO
    {"follow_u2i",  follow_u2i_recall, follow_u2i_prepare, follow_u2i_parse},
    {"recent_u2i",  recent_u2i_recall, recent_u2i_prepare, recent_u2i_parse}
};

} // namespace

void recall(Context& ctx) {
    auto& group_conf = ctx.exp_config->groups[ctx.group_id];
    auto& recall_config = group_conf.recall.channel_config;
    std::vector<std::string> redis_cmds;
    std::vector<std::string> redis_keys;
    std::unordered_map<std::string, size_t> idx_mapping;
    std::vector<std::vector<size_t>> channel_parse_idx(recalls.size());

    for (size_t i = 0; i < recalls.size(); i++) {
        auto& [channel, recall_func, prepare_func, parse_func] = recalls[i];
        auto conf_iter = recall_config.find(channel);
        if (conf_iter != recall_config.end() && conf_iter->second.enable && prepare_func != nullptr) {
            prepare_func(ctx, channel, conf_iter->second, redis_cmds, redis_keys, idx_mapping, channel_parse_idx[i]);
        }
    }

    if (redis_cmds.empty()) {
        ALOG(INFO, "no redis recall cmds, skip redis fetch and parse");
    } else {
        brpc::RedisResponse resp;
        redis::get(redis_cmds, redis_keys, resp);

        for (size_t i = 0; i < recalls.size(); i++) {
            auto& [channel, recall_func, prepare_func, parse_func] = recalls[i];
            auto conf_iter = recall_config.find(channel);
            if (conf_iter != recall_config.end() && conf_iter->second.enable && parse_func != nullptr) {
                parse_func(ctx, channel, resp, channel_parse_idx[i]);
            }
        }
    }

    for (size_t i = 0; i < recalls.size(); i++) {
        auto& [channel, recall_func, prepare_func, parse_func] = recalls[i];
        auto conf_iter = recall_config.find(channel);
        if (conf_iter != recall_config.end() && conf_iter->second.enable) {
            recall_func(ctx, channel, conf_iter->second);
        }
    }

    op_recall(ctx);
}

} // namespace rec

} // namespace predictionmarkets_rec
