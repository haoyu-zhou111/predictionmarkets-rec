#pragma once
#include <random>
#include <stack>
#include <string.h>
#include <murmurhash3/murmurhash3.h>
#include <nlohmann/json.hpp>

#include "common/log.h"

template <typename T>
T get_json(const nlohmann::json& j, const std::string& key, const T& default_val) {
    if (!j.contains(key)) {
        ALOG(WARNING, "param %s not found, use default val", key.c_str());
        return default_val;
    }

    try {
        return j[key].get<T>();
    } catch (...) {
        ALOG(WARNING, "param %s mismatch, use default val", key.c_str());
        return default_val;
    }
}

inline const nlohmann::json& get_json_obj(const nlohmann::json& j, const std::string& key) {
    static const nlohmann::json empty_obj = nlohmann::json::object();
    if (j.contains(key) && j[key].is_object()) {
        return j[key];
    }
    ALOG(WARNING, "json object not found: %s, use empty object", key.c_str());
    return empty_obj;
}

inline void json_merge(nlohmann::json& target, const nlohmann::json& delta) {
    std::stack<std::pair<nlohmann::json*, const nlohmann::json*>> st;
    st.emplace(&target, &delta);

    while(!st.empty()) {
        auto [t, d] = st.top();
        st.pop();

        if (t->is_object() && d->is_object()) {
            for (const auto& entry : d->items()) {
                const std::string& key = entry.key();
                const nlohmann::json& val = entry.value();

                auto iter = t->find(key);
                if (iter == t->end()) {
                    t->emplace(key, val);
                } else {
                    if (iter->is_object() && val.is_object()) {
                        st.emplace(&(*iter), &val);
                    } else {
                        *iter = val;
                    }
                }
            }
        } else {
            *t = *d;
        }
    }
}

template <typename T>
std::vector<T> sample(const std::vector<T>& vec, size_t count) {
    if (vec.empty() || count == 0) {
        return {};
    }

    thread_local std::mt19937 g(std::random_device{}());
    std::vector<T> tmp = vec;

    std::shuffle(tmp.begin(), tmp.end(), g);
    if (tmp.size() > count) {
        tmp.resize(count);
    }

    return tmp;
}

inline uint32_t feature_hash(const std::string& key, int hash_size) {
    uint64_t out[2];
    MurmurHash3_x64_128(key.data(), (int)key.size(), 0, out);
    uint64_t mask = (1ULL << hash_size) - 1;
    return out[0] & mask;
}
