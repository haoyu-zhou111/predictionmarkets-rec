#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/type_define.h"

namespace predictionmarkets_rec {

enum class ItemVisibility {
    UNKNOWN,
    PUBLIC,
    MEMBERS,
    PAID
};

struct Item {
    ItemId               id;
    std::string          title;
    std::string          slug;
    std::vector<CateId>  cates;                      // tag id 列表
    std::vector<UserId>  authors;                    // author id 列表
    ItemVisibility       visibility   = ItemVisibility::UNKNOWN;
    uint64_t             published_at = 0;            // Unix 秒戳（Ghost 发布时间）
    uint64_t             updated_at   = 0;            // Unix 秒戳
    uint64_t             exposure     = 0;            // 曝光数（bandit 的 n）
    uint64_t             click        = 0;            // 点击数（bandit 的 k）
};

// 自包含的一个池：items + 该池自己的热门/新内容索引
struct ItemPool {
    std::unordered_map<ItemId, Item> items;             // 池内全部 item
    std::vector<ItemId>              hot_items;          // 热门：items 按 Wilson score 降序，exposure=0 不入
    std::vector<ItemId>              new_items;          // 新内容：items 按 published_at 降序
};

// 免费池（visibility != PAID）与全量池，各自独立发布；每个请求按付费状态取其一。
// 两池互不要求一致，故各用一个原子 shared_ptr，分别 store。
extern std::shared_ptr<const ItemPool> g_free_pool;
extern std::shared_ptr<const ItemPool> g_all_pool;

bool item_pool_init();

bool item_pool_load();

} // namespace predictionmarkets_rec
