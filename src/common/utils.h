#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <stack>
#include <string>
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

inline const nlohmann::json& get_json_arr(const nlohmann::json& j, const std::string& key) {
    static const nlohmann::json empty_arr = nlohmann::json::array();
    if (j.contains(key) && j[key].is_array()) {
        return j[key];
    }
    ALOG(WARNING, "json array not found: %s, use empty array", key.c_str());
    return empty_arr;
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

inline uint32_t feature_hash(const std::string& key, uint32_t hash_size) {
    uint64_t out[2];
    MurmurHash3_x64_128(key.data(), (int)key.size(), 0, out);
    uint64_t mask = (1ULL << hash_size) - 1;
    return out[0] & mask;
}

// base64url 编码（无 padding），用于 JWT
inline std::string base64url_encode(const unsigned char* data, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t chunk = data[i] << 16;
        int rem = static_cast<int>(len - i);
        if (rem > 1) chunk |= data[i + 1] << 8;
        if (rem > 2) chunk |= data[i + 2];

        out.push_back(tbl[(chunk >> 18) & 0x3F]);
        out.push_back(tbl[(chunk >> 12) & 0x3F]);
        if (rem > 1) out.push_back(tbl[(chunk >> 6) & 0x3F]);
        if (rem > 2) out.push_back(tbl[chunk & 0x3F]);
    }
    return out;
}

inline std::string base64url_encode(const std::string& s) {
    return base64url_encode(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

// hex 字符串还原为字节，非法输入抛异常
inline std::string hex_decode(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        throw std::runtime_error("hex length is odd");
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("invalid hex char");
    };
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}

// Howard Hinnant 的 civil->days 算法，纯整数、无时区依赖
inline int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// ISO8601（形如 "2023-01-01T12:00:00.000Z"）转 Unix 秒（UTC），失败返回 0
inline uint64_t iso8601_to_epoch(const std::string& s) {
    int y = 0, mon = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mon, &d, &h, &mi, &sec) != 6) {
        return 0;
    }
    int64_t days = days_from_civil(y, static_cast<unsigned>(mon), static_cast<unsigned>(d));
    int64_t epoch = days * 86400 + h * 3600 + mi * 60 + sec;
    return epoch < 0 ? 0 : static_cast<uint64_t>(epoch);
}

// Beta(a, b) 采样：由两次 Gamma 采样组合 X/(X+Y) 得到，X~Gamma(a,1)、Y~Gamma(b,1)，单次 O(1)
inline double beta_sample(double a, double b) {
    thread_local std::mt19937 gen(std::random_device{}());
    std::gamma_distribution<double> ga(a, 1.0);
    std::gamma_distribution<double> gb(b, 1.0);
    double x = ga(gen);
    double y = gb(gen);
    double s = x + y;
    if (s <= 0.0) {
        return 0.0;
    }
    return x / s;
}

// Wilson score 置信下界（z=1.96），n=0 返回 0
inline double wilson_score(uint64_t n, uint64_t k) {
    if (n == 0) return 0.0;
    const double z = 1.96;
    const double z2 = z * z;
    double phat = static_cast<double>(k) / static_cast<double>(n);
    double denom = 1.0 + z2 / n;
    double center = phat + z2 / (2.0 * n);
    double margin = z * std::sqrt((phat * (1.0 - phat) + z2 / (4.0 * n)) / n);
    return (center - margin) / denom;
}
