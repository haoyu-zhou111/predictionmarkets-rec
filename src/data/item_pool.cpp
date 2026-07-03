#include <algorithm>
#include <cstdint>
#include <ctime>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <nlohmann/json.hpp>

#include "item_pool.h"
#include "common/config.h"
#include "common/log.h"
#include "common/redis.h"
#include "common/utils.h"

namespace predictionmarkets_rec {

using json = nlohmann::json;

std::shared_ptr<const ItemPool> g_free_pool;
std::shared_ptr<const ItemPool> g_all_pool;

namespace {

// 依据 Ghost Admin API Key（"id:secret"）生成短期 HS256 JWT
std::string make_ghost_jwt() {
    const std::string& admin_key = g_config.ghost.admin_key;
    size_t sep = admin_key.find(':');
    if (sep == std::string::npos) {
        ALOG(ERROR, "ghost admin_key must be in 'id:secret' form");
        throw std::runtime_error("ghost admin_key must be in 'id:secret' form");
    }
    std::string kid    = admin_key.substr(0, sep);
    std::string secret = hex_decode(admin_key.substr(sep + 1));

    int64_t now = static_cast<int64_t>(time(nullptr));

    json header  = {{"alg", "HS256"}, {"typ", "JWT"}, {"kid", kid}};
    json payload = {
        {"iat", now},
        {"exp", now + g_config.ghost.jwt_ttl_sec},
        {"aud", "/admin/"}
    };

    std::string signing_input = base64url_encode(header.dump()) + "." +
                                base64url_encode(payload.dump());

    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int mac_len = 0;
    HMAC(EVP_sha256(),
         secret.data(), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size(),
         mac, &mac_len);

    return signing_input + "." + base64url_encode(mac, mac_len);
}

ItemVisibility parse_visibility(const std::string& v) {
    if (v == "public")  return ItemVisibility::PUBLIC;
    if (v == "members") return ItemVisibility::MEMBERS;
    if (v == "paid")    return ItemVisibility::PAID;
    if (v == "tiers")   return ItemVisibility::PAID;   // 特定付费层级，按付费处理
    return ItemVisibility::UNKNOWN;
}

CURL* g_curl = nullptr;

size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* buf) {
    size_t len = size * nmemb;
    try {
        buf->append((char*)contents, len);
    } catch (...) {
        return 0;
    }
    return len;
}

bool curl_init() {
    g_curl = curl_easy_init();
    if (!g_curl) return false;

    curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(g_curl, CURLOPT_NOSIGNAL,      1L);
    curl_easy_setopt(g_curl, CURLOPT_TIMEOUT_MS,    g_config.ghost.timeout_ms);
    curl_easy_setopt(g_curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(g_curl, CURLOPT_FORBID_REUSE,  0L);
    return true;
}

std::string http_get(const std::string& url, const std::string& jwt) {
    std::string response;
    curl_easy_setopt(g_curl, CURLOPT_URL,       url.c_str());
    curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, &response);

    struct curl_slist* headers = nullptr;
    std::string auth = "Authorization: Ghost " + jwt;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept-Version: v5.0");
    curl_easy_setopt(g_curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(g_curl);
    curl_easy_setopt(g_curl, CURLOPT_HTTPHEADER, nullptr);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        throw std::runtime_error(curl_easy_strerror(res));
    }
    return response;
}

void parse_posts(const json& posts, ItemPool& all, ItemPool& free) {
    for (const auto& post : posts) {
        Item item;
        item.id = post.value("id", std::string{});
        if (item.id.empty()) continue;

        item.title      = post.value("title", std::string{});
        item.slug       = post.value("slug", std::string{});
        item.visibility = parse_visibility(post.value("visibility", std::string{}));
        item.created_at = iso8601_to_epoch(post.value("created_at", std::string{}));
        item.updated_at = iso8601_to_epoch(post.value("updated_at", std::string{}));

        // include=tags,authors 带回的关系数组，仅记录 id
        const auto tags_iter = post.find("tags");
        if (tags_iter != post.end() && tags_iter->is_array()) {
            for (const auto& tag : *tags_iter) {
                std::string tag_id = tag.value("id", std::string{});
                if (!tag_id.empty()) item.cates.emplace_back(std::move(tag_id));
            }
        }
        const auto authors_iter = post.find("authors");
        if (authors_iter != post.end() && authors_iter->is_array()) {
            for (const auto& author : *authors_iter) {
                std::string author_id = author.value("id", std::string{});
                if (!author_id.empty()) item.authors.emplace_back(std::move(author_id));
            }
        }

        bool is_free = item.visibility != ItemVisibility::PAID;
        all.items[item.id] = item;
        if (is_free) {
            free.items[item.id] = item;
        }
    }
}

// 分页拉取全部 status:published 的 post
void fetch_ghost_posts(ItemPool& all, ItemPool& free) {
    std::string jwt = make_ghost_jwt();
    const std::string base = g_config.ghost.admin_api_url + "/posts/"
        "?filter=status:published"
        "&fields=id,title,slug,visibility,created_at,updated_at"
        "&include=tags,authors"
        "&limit=" + std::to_string(g_config.ghost.page_limit);

    int page = 1;
    while (true) {
        std::string url = base + "&page=" + std::to_string(page);
        std::string body = http_get(url, jwt);

        json j = json::parse(body);
        if (j.contains("errors")) {
            throw std::runtime_error("ghost api error: " + j.dump());
        }

        const auto posts_iter = j.find("posts");
        if (posts_iter == j.end() || !posts_iter->is_array() || posts_iter->empty()) {
            break;
        }
        parse_posts(*posts_iter, all, free);

        // meta.pagination.next 为 null 时结束
        int64_t next = 0;
        try {
            const auto& pagination = j.at("meta").at("pagination");
            if (!pagination.at("next").is_null()) {
                next = pagination.at("next").get<int64_t>();
            }
        } catch (...) {
            next = 0;
        }
        if (next <= 0) break;
        page = static_cast<int>(next);
    }
}

// bandit 曝光/点击：每个 item 一个 hash（key = prefix + item_id），
// field 为 impression_total / click_total，用 pipeline 批量 HGETALL 后回填
void fill_bandit_stats(ItemPool& all, ItemPool& free) {
    if (all.items.empty()) {
        return;
    }

    std::vector<std::string> cmds;
    std::vector<std::string> keys;
    std::vector<ItemId>      ids;
    cmds.reserve(all.items.size());
    keys.reserve(all.items.size());
    ids.reserve(all.items.size());
    for (const auto& [id, item] : all.items) {
        cmds.emplace_back("hgetall");
        keys.emplace_back(g_config.bandit.post_stat_key_prefix + id);
        ids.emplace_back(id);
    }

    brpc::RedisResponse resp;
    if (!redis::get(cmds, keys, resp)) {
        ALOG(WARNING, "fetch bandit stats failed, keep exposure/click as 0");
        return;
    }

    const std::string& imp_field = g_config.bandit.impression_field;
    const std::string& clk_field = g_config.bandit.click_field;
    for (size_t i = 0; i < ids.size(); i++) {
        auto fields = redis::parse<std::unordered_map<std::string, std::string>>(resp.reply(i));

        uint64_t exposure = 0;
        uint64_t click    = 0;
        auto imp_iter = fields.find(imp_field);
        if (imp_iter != fields.end()) {
            try { exposure = std::stoull(imp_iter->second); } catch (...) {}
        }
        auto clk_iter = fields.find(clk_field);
        if (clk_iter != fields.end()) {
            try { click = std::stoull(clk_iter->second); } catch (...) {}
        }

        // 回填全量池与其 free 子集（免费池热门索引需要 free 池的 exposure/click）
        auto all_iter = all.items.find(ids[i]);
        if (all_iter != all.items.end()) {
            all_iter->second.exposure = exposure;
            all_iter->second.click    = click;
        }
        auto free_iter = free.items.find(ids[i]);
        if (free_iter != free.items.end()) {
            free_iter->second.exposure = exposure;
            free_iter->second.click    = click;
        }
    }
}

// 热门索引：预算好每个 item 的 Wilson score 再按其降序排，exposure=0 不入
std::vector<ItemId> build_hot_index(const std::unordered_map<ItemId, Item>& items) {
    std::vector<std::pair<ItemId, double>> scored;
    scored.reserve(items.size());
    for (const auto& [id, item] : items) {
        if (item.exposure == 0) continue;
        scored.emplace_back(id, wilson_score(item.exposure, item.click));
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::vector<ItemId> res;
    res.reserve(scored.size());
    for (auto& p : scored) {
        res.push_back(std::move(p.first));
    }
    return res;
}

// 新内容索引：按 created_at 降序
std::vector<ItemId> build_new_index(const std::unordered_map<ItemId, Item>& items) {
    std::vector<std::pair<ItemId, uint64_t>> scored;
    scored.reserve(items.size());
    for (const auto& [id, item] : items) {
        scored.emplace_back(id, item.created_at);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::vector<ItemId> res;
    res.reserve(scored.size());
    for (auto& p : scored) {
        res.push_back(std::move(p.first));
    }
    return res;
}

void build_index(ItemPool& pool) {
    pool.hot_items = build_hot_index(pool.items);
    pool.new_items = build_new_index(pool.items);
}

// 栈上构建免费池与全量池：拉取 Ghost post → 回填 bandit n/k → 各自建索引
void build_pools(ItemPool& all, ItemPool& free) {
    fetch_ghost_posts(all, free);
    fill_bandit_stats(all, free);
    build_index(all);
    build_index(free);
}

} // namespace

bool item_pool_load() {
    try {
        ItemPool all;
        ItemPool free;
        build_pools(all, free);

        auto all_ptr  = std::make_shared<ItemPool>(std::move(all));
        auto free_ptr = std::make_shared<ItemPool>(std::move(free));
        std::atomic_store(&g_all_pool,  std::shared_ptr<const ItemPool>(all_ptr));
        std::atomic_store(&g_free_pool, std::shared_ptr<const ItemPool>(free_ptr));

        ALOG(INFO, "item_pool load successfully, all=%lu free=%lu",
             all_ptr->items.size(), free_ptr->items.size());
        return true;
    } catch (const std::exception& e) {
        ALOG(WARNING, "item_pool_load failed: %s", e.what());
        return false;
    }
}

bool item_pool_init() {
    curl_global_init(CURL_GLOBAL_ALL);

    if (!curl_init()) {
        ALOG(ERROR, "item_pool curl init failed");
        return false;
    }

    try {
        while (true) {
            if (item_pool_load()) {
                ALOG(INFO, "item_pool init successfully");
                return true;
            }

            ALOG(ERROR, "item_pool init failed, retry after 1s");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        ALOG(ERROR, "item_pool init fatal error: %s", e.what());
        return false;
    }
}

} // namespace predictionmarkets_rec
