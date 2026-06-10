#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "user_feature.h"
#include "common/config.h"
#include "common/log.h"
#include "model.h"

namespace predictionmarkets_rec {

UserFeatureData g_user_feature;

bool user_feature_init() {
    try {
        while (true) {
            if (user_feature_load()) {
                ALOG(INFO, "user_feature init successfully");
                return true;
            }
            ALOG(ERROR, "user_feature init failed, retry after 1s");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        ALOG(ERROR, "user_feature init fatal error: %s", e.what());
        return false;
    }
}

bool user_feature_load() {
    const std::string& file_path = g_config.data_path.user_feature;
    std::ifstream ifs(file_path);

    if (!ifs.is_open()) {
        ALOG(ERROR, "open user_feature file failed: %s", file_path.c_str());
        return false;
    }

    std::string line;
    line.reserve(1024);

    while (std::getline(ifs, line)) {
        const char* p = line.data();
        const char* end = p + line.size();

        const char* id_start = p;
        while (p < end && *p != '|') p++;
        if (p >= end) continue;

        std::string_view id_view(id_start, p - id_start);
        UserId user_id = std::string(id_view);
        p++;

        auto& feature_map = g_user_feature[user_id];
        
        while (p < end) {
            const char* k_start = p;
            while (p < end && *p != ':') p++;
            if (p >= end) break;

            std::string_view key(k_start, p - k_start);
            p++;

            const char* v_start = p;
            while (p < end && *p != ';') p++;
            std::string_view val_str(v_start, p - v_start);
            feature_map[std::string(key)] = std::string(val_str);

            if (p < end && *p == ';') p++;
        }
    }

    ALOG(INFO, "load user_feature success, user count: %lu", g_user_feature.size());
    return true;
}

} // namespace predictionmarkets_rec
