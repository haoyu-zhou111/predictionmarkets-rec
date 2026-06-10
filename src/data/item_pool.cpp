#include <stdexcept>
#include <string>
#include <thread>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "item_pool.h"
#include "common/config.h"
#include "common/log.h"

namespace predictionmarkets_rec {

using json = nlohmann::json;

std::shared_ptr<const ItemPoolData> g_item_pool;

namespace {

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

    curl_easy_setopt(g_curl, CURLOPT_URL,           g_config.sync.available_item_api.c_str());
    curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(g_curl, CURLOPT_NOSIGNAL,      1L);
    curl_easy_setopt(g_curl, CURLOPT_TIMEOUT_MS,    g_config.sync.available_item_timeout_ms);
    curl_easy_setopt(g_curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(g_curl, CURLOPT_FORBID_REUSE,  0L);
    return true;
}

std::shared_ptr<ItemPoolData> try_load_item_pool() {
    std::string response;
    curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(g_curl);
    if (res != CURLE_OK) {
        throw std::runtime_error(curl_easy_strerror(res));
    }

    auto data = std::make_shared<ItemPoolData>();
    json j = json::parse(response);

    for (const auto& item : j["items"]) {
        data->item_set.insert(item);
    }

    return data;
}

} // namespace

bool item_pool_load() {
    try {
        auto new_data = try_load_item_pool();
        std::atomic_store(&g_item_pool, std::shared_ptr<const ItemPoolData>(new_data));
        ALOG(INFO, "item_pool load successfully, size %lu", new_data->item_set.size());
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
