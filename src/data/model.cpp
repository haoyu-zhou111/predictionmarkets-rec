#include <fstream>
#include <thread>

#include "model.h"
#include "common/config.h"

namespace predictionmarkets_rec {

LRModel g_model;

bool lr_model_init() {
    try {
        while (true) {
            if (lr_model_load()) {
                ALOG(INFO, "lr_model init successfully");
                return true;
            }
            ALOG(ERROR, "lr_model init failed, retry after 1s");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        ALOG(ERROR, "lr_model init fatal error: %s", e.what());
        return false;
    }
}

bool lr_model_load() {
    const std::string& model_path = g_config.model_path.ranking;

    std::ifstream fs(model_path, std::ios::binary);
    if (!fs) {
        ALOG(ERROR, "open model file failed: %s", model_path.c_str());
        return false;
    }

    fs.read(reinterpret_cast<char*>(&g_model.hash_size), sizeof(g_model.hash_size));
    fs.read(reinterpret_cast<char*>(&g_model.bias), sizeof(g_model.bias));
    if (fs.fail()) {
        ALOG(ERROR, "read model header failed: %s", model_path.c_str());
        return false;
    }

    g_model.weight.resize(1ULL << g_model.hash_size);
    fs.read(reinterpret_cast<char*>(g_model.weight.data()), (1ULL << g_model.hash_size) * 4LL);
    if (fs.fail()) {
        ALOG(ERROR, "read model weights failed: %s", model_path.c_str());
        return false;
    }

    return true;
}

} // namespace predictionmarkets_rec
