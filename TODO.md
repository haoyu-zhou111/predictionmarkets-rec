# TODO

记录待办事项，只记需要做什么，具体方案到时再评估。

- **推荐结果去重记录**：将每次返回给用户的推荐结果写入 redis，下次请求读取并在一段时间窗口内做去重（处理延迟曝光）。涉及写、读、去重时的使用、数据结构设计。
- **日志重定向到文件**：当前 `main.cpp` 中日志初始化被注释，日志未落盘，需要接入文件输出。
- **特征生产**：context 特征（`context_feature_map`）、行为特征等当前未生产，待数据就绪后接入。
- **曝光与点击拆分为两条 redis**：当前 `user_history` 一条列表内混用曝光与 `clicked` 字段，计划拆成独立的曝光列表和点击列表，曝光用于已曝光过滤、点击用于 i2i 召回 trigger。涉及上游约定、新增 config、`fetch_user_context` 读取与解析调整。（注：当前 `fetcher.cpp` 中 `if (j["clicked"])` 是临时写法，依赖该字段为 JSON 布尔，拆分后一并替换。）
- **item pool 改接 Ghost Admin API**：设计已定，待实现。
  - **Item 结构**：字段 `id`、`tags`（id 列表，待明日确认）、`authors`（id 列表，待明日确认）、`published_at`（int64_t Unix 秒戳）、`updated_at`（int64_t）、`status`（enum: PUBLISHED/DRAFT/UNKNOWN）、`visibility`（enum: PUBLIC/MEMBERS/PAID/UNKNOWN）、`n`、`k`（int64_t，bandit 用）。
  - **双 pool**：`visibility == PAID` 进 `paid_items`，其余进 `free_items`；`status != PUBLISHED` 跳过不入池。两个 pool 均为 `unordered_map<ItemId, Item>`。
  - **Ghost API 接入**：每次调用前用 Admin API Key（id + secret）生成短期 HS256 JWT；分页拉取，每页 100 条，循环直到无下一页。
  - **bandit n/k**：上游维护两个独立 Redis hash（分别存 n、k），同步时各做一次 `HGETALL`，合并写入 Item 对象。整个过程在后台同步线程完成，不上请求路径。
  - **下游影响**：召回过滤（`check_item`）、context 中的 pool 引用均需按 pool 区分处理。热门索引原来按 `cate_hot` 分区，改为按 tag 分区（`tag_hot_items`），索引构建逻辑从 `item_feature` 迁移到 `item_pool` 同步时完成（因为 tag 信息现在在 pool 里）。Wilson score 排序逻辑保持不变，n=0 的 item 不进入热门索引。
- **item_feature 完全关闭，相关引用全部迁移**：涉及以下文件，实现时一并处理：
  - `item_pool.h/cpp`：新 `Item` 结构体（含枚举）+ `ItemPoolData` 双 pool + 热门索引迁入；`build_hot_index()` 随每次 pool load 执行。
  - `context.h`：去掉 `item_feature` include 和字段；`recent_cates_*` 改名 `recent_tags_*`。
  - `rec_server.cpp`：去掉 `ctx.item_feature` 赋值。
  - `fetcher.cpp`：去掉 `item_feature_init()` 调用和 `run_item_feature` 线程；enrichment 循环改为从 `item_pool` 读取 Item 的 `tags`/`authors`。
  - `recall.cpp`：`global_hot_recall` 改读 `ctx.item_pool->global_hot_items`；`cate_hot_recall` 改读 `ctx.item_pool->tag_hot_items` 和 `ctx.recent_tags_vec`。
  - `rerank.cpp`：author/cate 查询改从 item_pool 双 pool 中查 Item，取 `authors[0]`/`tags[0]` 作为打散依据。
  - `filter.cpp`：`check_item` 改为检查 `free_items` 和 `paid_items` 两个 map。
  - `feature.cpp`：去掉 `ctx.item_feature->features` 查询（LR 特征工程待重设计，当前 bandit 阶段不调用）。
  - `config.h/cpp`：删除 `data_path.item_feature` 和 `sync.item_feature_interval_ms`。
- **分区热门索引（cate_hot / tag_hot）**：本期未建。item_pool 现已在 Item 上记录 tag id 列表（`cates`），后续在 item_pool 同步时按 tag 分区构建热门倒排索引（Wilson score 排序，exposure=0 不入），并恢复 recall 的 `cate_hot` 通道（当前函数体与分发表条目均已注释）。**免费池与全量池各建一份**（`g_free_pool` / `g_all_pool` 内各加一个分区热门索引，与现有 `hot_items`/`new_items` 一致）。
- **多 tag 打散**：rerank 打散当前只用 Item 的 `cates`/`authors` 列表首元素（主 tag/主 author）作键，后续可扩展为多 tag/多作者的打散语义。
- **按付费状态选池（双池设计）**：**已实现**——item_pool 拆成两个自包含 `ItemPool`（各带 `items`/`hot_items`/`new_items`），两个全局 `g_free_pool`/`g_all_pool` 各自原子发布（两池不要求一致，因每个请求只用其一）。proto 新增 `is_paid_user`；`rec_server` 按付费状态 `ctx.item_pool = is_paid ? g_all_pool : g_free_pool`，下游只认 `ctx.item_pool->items/hot_items/new_items`。**剩余**：上游调用方需实际填充 `is_paid_user`（未填默认 false→免费池）。
