#include <fstream>
#include <thread>

#include "i2i_index.h"
#include "common/log.h"

namespace predictionmarkets_rec {

std::unordered_map<std::string, I2IIndex> g_i2i_index_map;

bool i2i_index_init(const std::string& index_path, I2IIndex& index) {
    try {
        while (true) {
            if (i2i_index_load(index_path, index)) {
                ALOG(INFO, "i2i index %s init successfully", index_path.c_str());
                return true;
            }
            ALOG(ERROR, "i2i index %s init failed, retry after 1s", index_path.c_str());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        ALOG(ERROR, "i2i index %s init fatal error: %s", index_path.c_str(), e.what());
        return false;
    }
}

bool i2i_index_load(const std::string& index_path, I2IIndex& index) {
    std::ifstream ifs(index_path);

    if (!ifs.is_open()) {
        ALOG(ERROR, "open index file failed: %s", index_path.c_str());
        return false;
    }

    std::string line;
    line.reserve(20480);
    
    while (std::getline(ifs, line)) {
        const char* p = line.data();
        const char* end = p + line.size();

        const char* id_start = p;
        while (p < end && *p != ':') p++;
        if (p >= end) continue;

        std::string_view id_view(id_start, p - id_start);
        ItemId trigger = std::string(id_view);
        p++;

        auto& index_vec = index[trigger];

        while (p < end) {
            const char* item_start = p;
            while (p < end && *p != ',') p++;

            index_vec.emplace_back(item_start, static_cast<size_t>(p - item_start));

            if (p < end) p++;
        }
    }

    ALOG(INFO, "load i2i index %s success, item count: %lu", index_path.c_str(), index.size());
    return true;
}

} // namespace predictionmarkets_rec
