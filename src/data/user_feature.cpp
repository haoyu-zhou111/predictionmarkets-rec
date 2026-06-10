#include <fstream>
#include <stdexcept>
#include <string>

#include "user_feature.h"
#include "common/config.h"
#include "common/log.h"
#include "model.h"

namespace ratus_rec {

UserFeatureData g_user_feature;

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
            try {
                feature_map[std::string(key)] = std::string(val_str);
            } catch (...) {}
            
            if (p < end && *p == ';') p++;
        }
    }

    ALOG(INFO, "load user_feature success, user count: %lu", g_user_feature.size());
    return true;
}

} // namespace ratus_rec
