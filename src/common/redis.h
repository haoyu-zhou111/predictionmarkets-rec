#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <brpc/redis.h>
#include <brpc/channel.h>

namespace predictionmarkets_rec {

namespace redis {

bool init();

// 多参命令流水线：每条命令是一组分量 [cmd, arg1, arg2, ...]，按分量打包成 pipeline 一次下发；
// 调用方按 resp.reply(i) 逐条解析。避免 format 转义、支持变长命令。
bool exec(const std::vector<std::vector<std::string>>& commands, brpc::RedisResponse& resp);

// exec_batch：把 commands 按 window 分片，每片一个异步 CallMethod 并发下发，全部完成后返回。
// 用于后台大批量（如上千 key 的 HGETALL）——cluster 下一个 request 内多命令是串行子调用，
// 分片并发可把墙钟从 N×RTT 降到约 window×RTT。要求 commands 各条非空（下标按位置对齐）。
// replies[i] 指向第 i 条命令的 reply（顺序与输入一致）；某分片失败时其命令对应 replies 为 nullptr。
// responses 持有各片的 RedisResponse，需在使用 replies 期间保持存活。
struct BatchReplies {
    std::vector<std::unique_ptr<brpc::RedisResponse>> responses;   // 每片一个，为 replies 保活
    std::vector<const brpc::RedisReply*>              replies;     // 全局顺序，指向 responses 内的 reply
};

BatchReplies exec_batch(const std::vector<std::vector<std::string>>& commands, size_t window = 64);

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
