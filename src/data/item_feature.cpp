#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "item_feature.h"
#include "common/config.h"
#include "common/log.h"

namespace predictionmarkets_rec {

std::shared_ptr<const ItemFeatureData> g_item_feature;

namespace {

std::shared_ptr<ItemFeatureData> load_from_local() {
    const std::string& file_path = g_config.data_path.item_feature;
    std::ifstream ifs(file_path);

    if (!ifs.is_open()) {
        ALOG(ERROR, "open item_feature file failed: %s", file_path.c_str());
        return nullptr;
    }

    auto feat = std::make_shared<ItemFeatureData>();
    std::string line;
    line.reserve(1024);

    while (std::getline(ifs, line)) {
        const char* p = line.data();
        const char* end = p + line.size();

        const char* id_start = p;
        while (p < end && *p != '|') p++;
        if (p >= end) continue;

        std::string_view id_view(id_start, p - id_start);
        ItemId item_id = std::string(id_view);
        p++;

        auto& feature_map = feat->features[item_id];

        while (p < end) {
            const char* k_start = p;
            while (p < end && *p != ':') p++;
            if (p >= end) break;

            std::string_view key(k_start, p - k_start);
            p++;

            const char* v_start = p;
            while (p < end && *p != ';') p++;
            std::string_view val(v_start, p - v_start);
            feature_map[std::string(key)] = std::string(val);

            if (p < end && *p == ';') p++;
        }
    }

    ALOG(INFO, "load item_feature success, item count: %lu", feat->features.size());
    return feat;
}

// 热门索引已迁移至 item_pool（依据 bandit 曝光/点击算 Wilson score），
// item_feature 仅保留通用特征加载。cate/ctr 索引逻辑随之下线。
std::shared_ptr<ItemFeatureData> load_and_build_index() {
    return load_from_local();
}

} // namespace

bool item_feature_init() {
    // 本阶段（bandit 冷启动）不加载 item_feature，架子保留待后续启用。
    // try {
    //     while (true) {
    //         if (item_feature_load()) {
    //             ALOG(INFO, "item_feature init successfully");
    //             return true;
    //         }
    //         ALOG(ERROR, "item_feature init failed, retry after 1s");
    //         std::this_thread::sleep_for(std::chrono::seconds(1));
    //     }
    // } catch (const std::exception& e) {
    //     ALOG(ERROR, "item_feature init fatal error: %s", e.what());
    //     return false;
    // }
    ALOG(INFO, "item_feature init skipped (disabled in bandit phase)");
    return true;
}

bool item_feature_load() {
    auto data = load_and_build_index();
    if (!data) return false;

    std::atomic_store(&g_item_feature, std::shared_ptr<const ItemFeatureData>(data));
    return true;
}

bool item_feature_reload() {
    auto new_data = load_and_build_index();
    if (!new_data) {
        ALOG(WARNING, "item_feature reload failed, keep old version");
        return false;
    }

    std::atomic_store(&g_item_feature, std::shared_ptr<const ItemFeatureData>(new_data));
    ALOG(INFO, "item_feature reload success");
    return true;
}

} // namespace predictionmarkets_rec
