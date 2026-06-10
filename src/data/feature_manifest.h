#pragma once

#include <vector>
#include <string>

namespace predictionmarkets_rec {

struct FeatureManifest {
    std::vector<std::string> user_fea;
    std::vector<std::string> item_fea;
    std::vector<std::string> context_fea;

    std::vector<std::pair<std::string, std::string>> ui_cross_fea;
    std::vector<std::pair<std::string, std::string>> ii_cross_fea;

    std::vector<std::string> behavior_fea;
};

extern FeatureManifest g_feature_manifest;

bool feature_manifest_init();

bool feature_manifest_load();

} // namespace predictionmarkets_rec
