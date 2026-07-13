# predictionmarkets_rec 项目说明

## 一、项目目标

基于网页 feed 场景的轻量级推荐服务。优化目标为点击率（CTR），后续逐步引入其他互动行为，向多任务优化演进。设计原则：快速上线、稳定运行、低成本维护，不引入复杂模块。

---

## 二、整体架构

```
上游数据源层
  ├── 离线 SQL 表（天级/5分钟级离线文件）
  ├── Redis（实时用户行为、运营配置）
  └── 全量可用 item 接口（1秒轮询）

推荐服务层（本项目）
  ├── 内存存储（特征、模型、索引）
  ├── 后台同步线程（item pool、item feature、exp config）
  ├── Redis 交互模块
  ├── Context 管理
  └── 推荐主流程（召回 → 特征 → 精排 → 重排）

接口输出层
  └── brpc RPC 接口（RecommendService::recommend）
```

---

## 三、目录结构

```
src/
├── common/         # 配置、日志、Redis 客户端、工具函数
├── data/           # 内存数据结构（item pool、item/user feature、模型、i2i 索引、实验配置）
├── fetcher/        # 后台同步线程、用户上下文拉取
├── rec/            # 推荐主流程（recall、feature、rank、rerank、filter、context）
└── server/         # brpc 服务入口
proto/              # Protobuf 协议定义（对外接口契约，置于顶层便于外部消费）
conf/
├── config.conf     # 服务配置
└── exp.conf        # 实验分组配置（本地部分）
```

---

## 四、推荐主流程

请求进入后，通过 `thread_local Context ctx` 复用（每次请求调用 `ctx.reset()` 重置），全程读写 context，流程如下：

```
recall(ctx)          # 多路召回，结果写入 ctx.candidates
feature_combine(ctx) # 特征组合，写入 ctx.feature_idx / ctx.behavior_feature_vec
rank(ctx)            # LR 模型打分，写入 ctx.rank_scores
rerank(ctx)          # 打散 + 运营强插，写入 ctx.final_results
fill_response(ctx)   # 序列化返回结果
```

### 召回通道（7路）

| 通道 | 类型 | 说明 |
|------|------|------|
| `global_hot` | 热门 | item 特征加载时预建全局热度倒排 |
| `cate_hot` | 分区热门 | 按类别的热度倒排 |
| `icf` | I2I | 基于协同过滤的离线 i2i 索引 |
| `i2v` | I2I | 基于 item2vec 的离线 i2i 索引 |
| `swing` | I2I | 基于 swing 的离线 i2i 索引 |
| `follow_u2i` | U2I | 关注用户发布内容召回 |
| `recent_u2i` | U2I | 近期浏览作者发布内容召回 |

I2I 通道以用户历史有点击行为的 item 为 trigger。

召回阶段批量拉取 Redis，`idx_mapping` 做 key 去重，每路通道维护独立的 `parse_idx` 切片（`channel_parse_idx[i]`）。

### 过滤

`check_item()` 依次过滤：不在 item pool 中、在用户黑名单中、已曝光过。

### 特征

- 用户侧特征、item 侧特征：启动时从本地文件加载到内存
- 交叉特征：ui_cross（用户×item）、ii_cross（item×item）
- 行为特征：召回通道来源（channel binary 编码）
- 特征哈希：MurmurHash3，hash_size 由模型文件指定

### 精排

LR 模型，Sigmoid 输出分数。模型文件二进制格式：`hash_size (int)` + `bias (float)` + `weights (float[])`。

### 重排

- 运营强插（`op`）：从实验配置读取指定 item 和插入位置，优先占位
- 规则打散：`author_gap` / `cate_gap` 控制同作者/同类目 item 的最小间隔
- Bandit（冷启动阶段）：汤普森采样分作为底层排序分，优先级低于以上两者（详见第十节）

---

## 五、实时性设计

| 数据 | 更新频率 | 方式 |
|------|---------|------|
| 可用 item + 曝光/点击数 `(n, k)` | 1 秒 | 轮询上游接口，atomic 替换 `shared_ptr` |
| item 特征 | 5 分钟 | 重新加载本地文件，atomic 替换 `shared_ptr` |
| 实验配置 | 1 分钟 | 合并本地 + Redis 配置 |
| 用户特征 | 天级 | 服务启动时加载 |
| 排序模型 | 天级 | 服务启动时加载 |
| 用户黑名单 | 实时 | 每次请求从 Redis 读取 |

---

## 六、Redis Key 规范

| Key | 说明 |
|-----|------|
| `user_black:{user_id}` | 用户黑名单（set） |
| `user_history:{user_id}` | 用户曝光历史（list，JSON，含 `item_id`、`clicked`） |
| `user_followed:{user_id}` | 用户关注列表（list） |
| `user_published:{user_id}` | 用户（作者）发布内容（list） |

`lrange` 命令拉取全量列表（`0 -1`）。

---

## 七、实验分组

用户按 `MD5(user_id + "_predictionmarkets") % 10` 分为 10 组（group_0 ~ group_9）。

实验配置两层合并：`base`（全量默认）→ `group_X`（分组覆盖）。本地 `conf/exp.conf` 与 Redis 中的远端配置合并，远端优先级更高。

---

## 八、上游依赖

### 离线 SQL 表

| 表名 | 说明 |
|------|------|
| `user_info` | 用户宽表（人口属性等静态特征） |
| `item_info` | item 宽表，含 `status` 字段标记是否可用 |
| `pv` | 曝光表，含 `user_id`、`item_id`、`timestamp`、`duration`、`request_id` |
| `action` | 行为表（暂未接入） |
| `follow` | 关注状态表 |
| `blacklist` | 黑名单表（对作者/类别的拉黑） |

### 专用接口

全量可用 item 接口：服务每 1 秒拉取，返回当前 `status` 可用的所有 `item_id`，同时携带每个 item 的曝光数 `n` 和点击数 `k`。

---

## 九、开发规范

### 分支策略
- 所有开发在新 branch 上进行，命名如 `fix/xxx`、`feat/xxx`、`style/xxx`
- 经确认后合并到 master，不直接在 master 上修改

### 代码风格（C++）
- 缩进：4 个空格
- 引用/指针符号靠近类型：`Context& ctx`，`int* p`
- 命名：变量/函数 snake_case，类/结构体 PascalCase，常量 UPPER_SNAKE_CASE
- 类型别名统一用 `using`
- namespace 结束注释：`} // namespace predictionmarkets_rec`
- include 顺序：标准库 `<>` → 三方库 `<>` → 项目头文件 `""`
- 重量级模块（config、context、召回通道配置）的并列字段做列对齐

### 编译
Linux 环境，依赖 brpc、Protobuf、OpenSSL、curl。

```bash
bash build.sh
```

输出二进制：`./predictionmarkets_rec`，配置文件目录：`./conf/`。

---

## 十、Bandit 冷启动策略

### 背景

产品优化目标为点击率。早期数据量不足以训练特征与排序模型，使用汤普森采样以点击事件为 reward 完成冷启动阶段的探索与利用。

### 算法：汤普森采样

以点击事件为 Bernoulli 分布（有点击 reward=1，无点击 reward=0），先验取 Beta(1, 1)。经过 n 次曝光、k 次点击后，后验为：

```
Beta(1 + k, 1 + n - k)
```

推荐请求到来时，对所有候选 item 在各自的 Beta 后验上采样，采样值即为该 item 的预估点击率，作为排序分写入 `ctx.rank_scores`，后续 rerank 照常执行。Beta 采样通过两次 Gamma 采样组合得到，单次 O(1)，无性能负担。

### 与主流程的集成

bandit 启用时，`rank()` 步骤跳过，bandit 采样直接填充 `ctx.rank_scores`：

```
recall(ctx)          # 不变
feature_combine(ctx) # bandit 阶段跳过（无模型时无需特征）
rank(ctx)            # bandit 启用时跳过
rerank(ctx)          # 不变，见下方优先级
fill_response(ctx)   # 不变
```

重排内部优先级：**坑位强插（op）> 规则打散 > bandit 排序**

### 工程方案

上游在提供全量可用 item 接口时同时携带每个 item 的曝光数 `n` 和点击数 `k`，推荐服务在 1 秒轮询时一并拉取，与 item pool 同步更新。item pool 不携带分区等内容信息。

热门索引（`global_hot_items` / `cate_hot_items`）在 item_feature 加载时（5分钟）构建，排序依据从特征文件中的 `ctr` 字段改为利用 item_pool 中的曝光数 `n` 和点击数 `k` 计算出的 Wilson score lower bound。`ctr` 特征本身不受影响，仅热门索引的排序逻辑替换。n=0 的 item 不进入热门索引。新内容召回后续单独通过 publishtime 索引处理。

### 迭代计划

| 阶段 | 排序策略 | 前提条件 |
|------|---------|---------|
| 阶段一（当前） | 纯汤普森采样 | 无特征与模型 |
| 阶段二 | 模型精排为主，必要时定坑或分数融合探索新内容 | 有初版特征与模型 |
| 阶段三+ | 召回与精排模型持续迭代，暂不引入 Linear UCB 等复杂策略 | — |
