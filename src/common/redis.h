#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <brpc/redis.h>
#include <brpc/channel.h>

namespace predictionmarkets_rec {

namespace redis {

bool init();

bool get(const std::vector<std::string>& cmds, const std::vector<std::string>& keys, brpc::RedisResponse& resp);

bool set(const std::vector<std::string>& cmds, const std::vector<std::string>& keys, const std::vector<std::string>& values);

template <typename T>
T parse(const brpc::RedisReply& r);

//get
template <>
inline std::string parse<std::string>(const brpc::RedisReply& r) {
    return r.is_string() ? std::string(r.data()) : "";
}

//lrange
template<>
inline std::vector<std::string> parse<std::vector<std::string>>(const brpc::RedisReply& r) {
    std::vector<std::string> res;
    if (r.is_array()) {
        for (size_t i = 0; i < r.size(); ++i) {
            res.push_back(std::string(r[i].data()));
        }
    }
    return res;
}

//smembers
template<>
inline std::unordered_set<std::string> parse<std::unordered_set<std::string>>(const brpc::RedisReply& r) {
    std::unordered_set<std::string> res;
    if (r.is_array()) {
        for (size_t i = 0; i < r.size(); ++i) {
            res.insert(std::string(r[i].data()));
        }
    }
    return res;
}

//hgetall（返回 [field, value, field, value, ...]）
template<>
inline std::unordered_map<std::string, std::string> parse<std::unordered_map<std::string, std::string>>(const brpc::RedisReply& r) {
    std::unordered_map<std::string, std::string> res;
    if (r.is_array()) {
        for (size_t i = 0; i + 1 < r.size(); i += 2) {
            res.emplace(std::string(r[i].data()), std::string(r[i + 1].data()));
        }
    }
    return res;
}


} // namespace redis

} // namespace predictionmarkets_rec
