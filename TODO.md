# TODO

记录待办事项，只记需要做什么，具体方案到时再评估。

- **推荐结果去重记录**：将每次返回给用户的推荐结果写入 redis，下次请求读取并在一段时间窗口内做去重（处理延迟曝光）。涉及写、读、去重时的使用、数据结构设计。
- **日志重定向到文件**：当前 `main.cpp` 中日志初始化被注释，日志未落盘，需要接入文件输出。
- **特征生产**：context 特征（`context_feature_map`）、行为特征等当前未生产，待数据就绪后接入。
- **热门索引迁移到 item_pool**：当前 `global_hot_items` / `cate_hot_items` 在 item_feature 加载（5分钟）时按特征文件的 `ctr` 字段排序构建。后续计划在 1 秒同步 item_pool 时让上游一并带回分区（cate）与发布时间（publishtime），把热门索引统一放到 item_pool 侧构建，排序依据改为用 `show`/`click` 算出的 Wilson score lower bound（`utils.h::wilson_lower_bound` 已就绪），并顺便建立新内容（按 publishtime）索引。涉及上游接口约定扩展、item_pool 数据结构、热门/新内容索引构建位置调整、recall 读取来源调整。
- **曝光与点击拆分为两条 redis**：当前 `user_history` 一条列表内混用曝光与 `clicked` 字段，计划拆成独立的曝光列表和点击列表，曝光用于已曝光过滤、点击用于 i2i 召回 trigger。涉及上游约定、新增 config、`fetch_user_context` 读取与解析调整。（注：当前 `fetcher.cpp` 中 `if (j["clicked"])` 是临时写法，依赖该字段为 JSON 布尔，拆分后一并替换。）
