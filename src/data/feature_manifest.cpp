#include <fstream>
#include <thread>

#include "feature_manifest.h"
#include "common/config.h"
#include "common/log.h"
#include "common/type_define.h"

namespace predictionmarkets_rec {

FeatureManifest g_feature_manifest;

namespace {
void parse_single(const json& json_manifest, std::vector<std::string>& feature_vec) {
    if (!json_manifest.is_array()) return;
    for (const auto& fea_name : json_manifest) {
        if (fea_name.is_string()) {
            feature_vec.push_back(fea_name.get<std::string>());
        }
    }
}

void parse_pair(const json& json_manifest, std::vector<std::pair<std::string, std::string>>& feature_vec) {
    if (!json_manifest.is_array()) return;
    for (const auto& pair : json_manifest) {
        if (!pair.is_array() || pair.size() != 2) continue;
        const auto& first = pair[0];
        const auto& second = pair[1];
        if (first.is_string() && second.is_string()) {
            feature_vec.emplace_back(first.get<std::string>(), second.get<std::string>());
        }
    }
}

} // namespace

bool feature_manifest_init() {
    try {
        while (true) {
            if (feature_manifest_load()) {
                ALOG(INFO, "feature_manifest init successfully");
                return true;
            }
            ALOG(ERROR, "feature_manifest init failed, retry after 1s");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        ALOG(ERROR, "feature_manifest init fatal error: %s", e.what());
        return false;
    }
}

bool feature_manifest_load() {
    const std::string& file_path = g_config.data_path.feature_manifest;
    std::ifstream ifs(file_path);

    if (!ifs.is_open()) {
        ALOG(ERROR, "open feature manifest file failed: %s", file_path.c_str());
        return false;
    }

    try {
        json j;
        ifs >> j;
        
        parse_single(get_json_arr(j, "user"), g_feature_manifest.user_fea);
        parse_single(get_json_arr(j, "item"), g_feature_manifest.item_fea);
        parse_single(get_json_arr(j, "context"), g_feature_manifest.context_fea);
        parse_pair(get_json_arr(j, "ui_cross"), g_feature_manifest.ui_cross_fea);
        parse_pair(get_json_arr(j, "ii_cross"), g_feature_manifest.ii_cross_fea);
        parse_single(get_json_arr(j, "behavior"), g_feature_manifest.behavior_fea);

        ALOG(INFO, "load feature manifest success, user: %lu, item: %lu, context: %lu, ui_cross: %lu, ii_cross: %lu, behavior: %lu",
            g_feature_manifest.user_fea.size(),
            g_feature_manifest.item_fea.size(),
            g_feature_manifest.context_fea.size(),
            g_feature_manifest.ui_cross_fea.size(),
            g_feature_manifest.ii_cross_fea.size(),
            g_feature_manifest.behavior_fea.size()
        );
        return true;
    } catch (const std::exception& e) {
        ALOG(ERROR, "load feature manifest error: %s", e.what());
        return false;
    }
}

} // namespace predictionmarkets_rec
