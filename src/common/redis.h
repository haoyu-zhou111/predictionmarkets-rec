#pragma once
#include <brpc/redis.h>
#include <brpc/channel.h>

namespace ratus_rec {

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
        for (uint32_t i = 0; i < r.size(); ++i) {
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
        for (uint32_t i = 0; i < r.size(); ++i) {
            res.insert(std::string(r[i].data()));
        }
    }
    return res;
}


} // namespace redis

} // namespace ratus_rec
