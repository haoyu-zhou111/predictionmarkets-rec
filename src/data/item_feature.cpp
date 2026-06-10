#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>

#include "item_feature.h"
#include "common/config.h"
#include "common/log.h"

namespace ratus_rec {

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
            try {
                feature_map[std::string(key)] = std::string(val);
            } catch (...) {}

            if (p < end && *p == ';') p++;
        }
    }

    ALOG(INFO, "load item_feature success, item count: %lu", feat->features.size());
    return feat;
}

void build_global_hot(std::shared_ptr<ItemFeatureData>& feat) {
    std::vector<std::pair<ItemId, double>> items;

    for (const auto& entry : feat->features) {
        auto ctr_iter = entry.second.find("ctr");
        if (ctr_iter == entry.second.end()) continue;
        int ctr_bucket = -1;
        try {
            ctr_bucket = stoi(ctr_iter->second);
        } catch (...) {}
        items.emplace_back(entry.first, ctr_bucket);
    }

    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    feat->global_hot_items.clear();
    for (const auto& p : items) {
        feat->global_hot_items.push_back(p.first);
    }
}

void build_cate_hot(std::shared_ptr<ItemFeatureData>& feat) {
    std::unordered_map<CateId, std::vector<std::pair<ItemId, double>>> cate_map;

    for (const auto& entry : feat->features) {
        const auto& f = entry.second;
        auto cate_iter = f.find("cate");
        auto ctr_iter = f.find("ctr");

        if (cate_iter == f.end() || ctr_iter == f.end()) continue;

        CateId c = static_cast<CateId>(cate_iter->second);
        int ctr_bucket = -1;
        try {
            ctr_bucket = stoi(ctr_iter->second);
        } catch (...) {}
        cate_map[c].emplace_back(entry.first, ctr_bucket);
    }

    feat->cate_hot_items.clear();
    for (auto& entry : cate_map) {
        auto& v = entry.second;
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        std::vector<ItemId> res;
        for (auto& p : v) {
            res.push_back(p.first);
        }

        feat->cate_hot_items[entry.first] = std::move(res);
    }
}

std::shared_ptr<ItemFeatureData> load_and_build_index() {
    auto feat = load_from_local();
    if (!feat) return nullptr;

    build_global_hot(feat);
    build_cate_hot(feat);

    ALOG(INFO, "build hot index success, global hot: %lu", feat->global_hot_items.size());
    return feat;
}

} // namespace

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

} // namespace ratus_rec
