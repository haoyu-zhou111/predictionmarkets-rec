# TODO

记录待办事项，只记需要做什么，具体方案到时再评估。

## 阶段二（有特征 / 模型之后）

- **特征生产**：context 特征（`context_feature_map`）、行为特征等当前未生产，待数据就绪后接入。
- **曝光与点击拆分为两条 redis**：当前 `user_history` 一条列表内混用曝光与 `clicked` 字段，计划拆成独立的曝光列表和点击列表，曝光用于已曝光过滤、点击用于 i2i 召回 trigger。涉及上游约定、新增 config、`fetch_user_context` 读取与解析调整。（注：当前 `fetcher.cpp` 中 `if (j["clicked"])` 是临时写法，依赖该字段为 JSON 布尔，拆分后一并替换。）
- **分区热门索引（cate_hot / tag_hot）**：本期未建。item_pool 已在 Item 上记录 tag id 列表（`cates`），后续在 item_pool 同步时按 tag 分区构建热门倒排索引（Wilson score 排序，exposure=0 不入），并恢复 recall 的 `cate_hot` 通道（当前函数体与分发表条目均已注释）。**免费池与全量池各建一份**（`g_free_pool` / `g_all_pool` 内各加一个分区热门索引，与现有 `hot_items`/`new_items` 一致）。
- **时效（新内容）召回通道**：item_pool 已建 `new_items` 索引（created_at 降序），但暂无召回通道读它。后续仿 `global_hot_recall` 加一个新内容召回通道（读 `ctx.item_pool->new_items`）。当前召回只开 `full`（全量喂汤普森，已覆盖 n=0 新内容），故本阶段不需要。

## 上线对接（推荐代码之外，需上游 / 运维配合）

- **反向代理（nginx）**：浏览器直连推荐服务受混合内容 + CORS 限制，需运营配一层 nginx：HTTPS 证书 / TLS 终结 + CORS 白名单（`www`/`test` × `informarket`/`predictionmarkets` 共 4 个 origin）+ 反代到 brpc `:15555`。推荐服务不做 CORS。
- **生产 redis 写入 exp 配置 key**：`config.prod.conf` 的 `exp.redis_key` 已对齐为 `predictionmarkets:exp_config`，但生产 redis 集群里该 key 尚未写入。上线前需由管 prod 的同学写入（至少一份空配置 `{"base":{},"group_0":{},...,"group_9":{}}`），否则服务起来会走 empty fallback 并刷 "remote exp config empty" warning（功能不受影响）。测试环境已写好。
- **user_context redis key 加命名空间前缀**：exp key 已用 `predictionmarkets:` 前缀，但 `user_context` 的 `user_black:` / `user_history:` / `user_followed:` / `user_published:` 仍是老名字、无前缀。这些 key 当前未实际使用，等接入用户上下文时一并统一加前缀（config + 上游写入方约定）。

## 零散（不急）

- **recent_u2i / recent_authors 填充顺序**：`click_list` 为旧→新，`fetcher.cpp` 构建 `recent_authors_vec` 时从前往后（旧→新）遍历，`recent_u2i_prepare` 又取前 `trigger_count` 个，结果取到的是**最旧**的作者，与 "recent" 语义相反。i2i 召回本次已改为从后往前取最近点击（见 `i2i_recall`），recent_u2i 侧应对齐为"取最近作者"（改遍历方向或取尾部）。当前 `recent_u2i` enable=false，不影响线上。
- **多 tag 打散**：rerank 打散当前只用 Item 的 `cates`/`authors` 列表首元素（主 tag/主 author）作键，后续可扩展为多 tag/多作者的打散语义。
- **召回通道贡献统计（DEBUG log）**：想在 `recall` 分发循环里按通道打贡献统计，但正确指标应为**每通道总召回量**与**独占比**（仅该通道召回、其余通道均无的 item 占比），而非按分发顺序的"增量"（增量受通道先后影响，靠后的通道被低估、不准）。独占比需要每 item 的精确通道来源，但当前 `ctx.channels` 只存通道**分组** bit（hot/i2i/u2i/op，且 `global_hot`/`cate_hot`/`full` 共用 bit 0），粒度不够，需先补 per-channel 来源记录再统计。
