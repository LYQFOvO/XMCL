# XMCL 批次 B：模组管理实施计划（交给 DeepSeek）

> **执行方式：**按 B1 → B2 → B3 顺序逐任务实施，每个任务包含测试、实现和验收。若执行环境提供 `executing-plans` 技能，使用它按检查点执行。本文只交接计划，不要求执行者使用多代理，也不授权发布或推送仓库。

**Goal：**完成 F18–F23、F30，让玩家从现有实例入口扫描模组、看详情、批量启停、锁定、检测重复、预览更新、补前置、切换版本，并在修改失败后恢复原文件。

**Architecture：**一个 `ModService` 接入现有 `FeatureService` / `ServiceHub`；解析、目录保护、文件事务、远端查询各有小型 helper。UI 保留现有主界面，用独立模组管理弹窗与更新确认弹窗完成操作。所有文件修改经统一保护和事务提交。

**Tech Stack：**C++17、Qt 6.7+、Qt Network、Qt Quick/QML、Qt Test、libarchive；TOML 使用固定版本 toml++，不手写正则解析器。

**项目根目录：**`/Volumes/olo/XMCL`。下面所有 `src/`、`ui/`、`tests/` 路径均相对此根目录。

**整理日期：**2026-10-27。本文依据当前文件静态阅读；没有为本计划修改应用代码或运行构建。旧总览：[KAMUCL 功能与实现指南](../../KAMUCL-FEATURES-FOR-DEEPSEEK.md)。

## 0. 当前基线与全局约束

### 0.1 已有实现，必须复用

| 位置 | 已有内容 | 批次 B 的处理 |
| --- | --- | --- |
| `src/services/ResourceService.*` | `resource.list/import/toggle/remove/duplicates` | 保留资源包、世界、服务器能力；mods 写操作逐步转统一事务 |
| `src/services/InstanceService.*` | `instancePath()`、`gamePath()`、实例配置与隔离 | 只通过此服务解析实例目录，不能用显示名拼路径 |
| `src/services/CommunityService.*` | Modrinth/CurseForge 查询，Modrinth required 依赖下载 | 保留现有社区弹窗；接入目录保护，复用已验证下载逻辑 |
| `src/services/DownloadEngine.*` | `.part`、摘要、大小、暂停、取消、回退 | 新下载写 staging，不直接写活动 mods |
| `src/services/GameService.*` | 游戏 QProcess、启动检查与修复 | 增加启动租约，禁止与模组提交同时进行 |
| `src/services/JavaRuntimeService.*`、`ui/LaunchCheckDialog.qml` | 批次 A 的本地 Java 与启动检查 | 不重写、不移除 |
| `src/services/ArchiveUtils.*` | libarchive、路径与链接限制 | 新增只读限额读取 ZIP 条目 helper；不能扫描时整体解压 JAR |
| `ui/Main.qml`、`LibraryPage.qml`、`InstanceCard.qml` | 实例选择、管理与设置入口 | 主布局不变，实例设置内增加“管理模组”入口 |
| `ui/GlassComboBox.qml`、`Theme.qml` | 可读的深色下拉控件 | 所有新增选择器复用 |
| `CMakeLists.txt` | 服务文件 GLOB；QML 明确列举；当前 13 个 CTest 套件 | 新 QML 显式加入；测试增量注册，不删旧测试 |

**当前缺口：**ResourceService 的列表是文件级信息；重复检测按完整文件 SHA-256，尚不能区分同 modId 不同版本；启停只处理单文件；没有完整的模组锁、更新计划、历史恢复事务。`gameStateChanged(bool)` 没有目标目录信息，单靠它不足以保护共享 mods 目录。

批次 A 文档也明确保留了 Java 自动下载、模块级健康检查、全量文件哈希以及真实游戏启动验收等缺口。批次 B 不偷偷扩大范围补 A，也不能把 A 宣称为全部完成。

### 0.2 边界

- 保留森林背景、圆角、玻璃效果、首页和侧栏结构。不要把日常操作继续塞进需要手填 UUID 的通用表单。
- 新界面最低适配 `1060×720`；长内容滚动；错误提示不被底部按钮遮住。
- 只管理当前实例解析到的 mods 目录；共享目录按真实目录身份互斥，不按 instanceId 各自上锁。
- 只读扫描不创建 mods 目录，不修改 JAR、instance.json 或用户配置；允许将缓存写入启动器自己的缓存目录。
- 不执行 JAR，不加载其中的 class，不用 `java -jar` 获取元数据。
- 不为了“兼容”默默装最新版；未知依赖/版本表达式显示“无法判断”，不显示绿色兼容。
- 大文件扫描、哈希、备份、复制在工作线程；网络对象在其所属线程使用；QML 只消费结果。
- 不扩展到 VoxLink、更新分发、F24 跨 Minecraft 版本迁移、整合包升级等其他批次。
- 发布、远端推送、用户目录批量清理不在本计划范围。参考仓库里的发布指令不适用于 XMCL。

### 0.3 三次交付

| 交付 | 范围 | 必须做到 | 暂不显示的按钮 |
| --- | --- | --- | --- |
| B1 | F18、F20 的只读部分 | 扫描、详情、搜索、兼容性、实例内/跨实例重复报告 | 启停、更新、补前置、恢复 |
| B2 | F19、F30；F20 可恢复清理 | 批量启停、版本锁、恢复点、恢复；全入口目录保护 | 更新、切换版本、在线补前置 |
| B3 | F21、F22、F23 | 可信来源识别、更新/补前置/版本切换计划、下载校验与事务提交 | 不做空按钮占位 |

一次交付一个可用增量。B1 可以先交付给用户体验；B2 不能在只有“成功路径”测试时启用写操作。

## 1. 文件安排与职责

旧总览中的文件名只是建议，本计划收敛为 **一个 UI 服务 + 专用 helper**，避免扫描服务、更新服务各自维护一份不一致的文件状态。

| 新文件 | 责任 | 首次需要 |
| --- | --- | --- |
| `src/services/ModTypes.h` | 目录快照、文件、descriptor、依赖等值类型 | B1 |
| `src/services/ModMetadata.h/.cpp` | 有界读取 JAR 与元数据规范化 | B1 |
| `src/services/ModVersionRange.h/.cpp` | 按 loader 语义求三态兼容结果 | B1 |
| `src/services/ModCatalog.h/.cpp` | 后台目录扫描、摘要、重复分组 | B1 |
| `src/services/ModService.h/.cpp` | actions、请求关联、异步调度、任务状态 | B1 |
| `ui/ModsDialog.qml` | 列表、详情、过滤、操作与恢复点页签 | B1 起增量增加 |
| `src/services/ModDirectoryGuard.h/.cpp` | 跨服务、跨实例、跨 XMCL 进程目录租约 | B2 |
| `src/services/ModStateStore.h/.cpp` | 文件/项目身份、锁定、来源记录与 revision | B2 |
| `src/services/ModFileTransaction.h/.cpp` | 原文件保护、提交、回滚、启动恢复、恢复点索引 | B2 |
| `src/services/ModRemoteClient.h/.cpp` | 远端文件/项目查询，输出统一候选结构 | B3 |
| `src/services/ModPlanner.h/.cpp` | 兼容候选、依赖闭包、变更计划与过期校验 | B3 |
| `ui/ModChangeDialog.qml` | 更新/补前置/版本变更确认 | B3 |
| `tests/TestModFixtures.h` | 在临时目录生成真实 ZIP/JAR 与文件辅助函数 | B1 |
| `tests/TestModServer.h` | 本机 HTTP API/CDN 夹具；延迟、坏 hash、断流 | B3 |
| `tests/tst_mods.cpp` | 解析、范围、扫描、重复、锁定、接口 | B1 起增量增加 |
| `tests/tst_mod_transactions.cpp` | 互斥、保护、故障注入、恢复 | B2 |
| `tests/tst_mod_remote.cpp` | 更新、候选、依赖、取消与过期 | B3 |
| `docs/migration/BATCH-B-MODS.md` | 实际实现/验证记录 | 每次交付更新 |

只在对应任务真正需要时创建文件。现有修改主要在 `ServiceHub.*`、`GameService.*`、`ResourceService.*`、`CommunityService.*`、`InstanceService.*`、`BridgeService.*`、`Main.qml`、`InstanceSettingsDialog.qml`、`DownloadsPage.qml`、`tests/tst_ui.cpp` 和 CMake。

## 2. 固定数据约定，先统一再写 UI

### 2.1 物理文件与逻辑模组分开

一个 JAR 可能声明多个 modId；一次启停/替换针对物理文件，详情和依赖针对 descriptor。不要把多 descriptor 展开成能分别删除的“文件”。

`ModTypes.h` 至少定义以下字段；这是实施时要建立的接口，目前尚不存在：

```cpp
#pragma once
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QByteArray>

namespace Mods {
enum class Match { Yes, No, Unknown };
enum class Relation { Required, Optional, Incompatible };

struct Dependency {
    QString id;                 // 本地 modId，不等于远端 projectId
    QString expression;         // 保留来源版本表达式
    QString dialect;            // fabric / quilt / maven / legacy
    Relation relation = Relation::Required;
    QString side = "both";      // client / server / both / unknown
    QVariantMap raw;            // 保存不能无损折叠的 any/all/条件等
};
struct Descriptor {
    QString id, name, version, loader;
    QStringList authors;
    QVector<Dependency> dependencies;
    QStringList provides;
    QString description;
    QStringList warnings;
};
struct File {
    QString fileId;             // 本次目录内定位 ID，不能作为任意路径
    QString fileName;           // 单层名称 .jar 或 .jar.disabled
    qint64 size = 0;
    qint64 mtimeMs = 0;
    QByteArray sha1, sha256;     // 小写 hex，网络匹配/本地快照使用
    bool enabled = true;
    bool locked = false;
    QVector<Descriptor> descriptors;
    QString error;
    QStringList warnings;
};
struct Snapshot {
    QString instanceId;
    QString directoryKey;       // 规范化游戏目录身份，不是显示路径
    QString snapshotId;         // 内容 + 名称 + 状态 revision 的摘要
    QString minecraftVersion, loader;
    quint64 stateRevision = 0;
    QVector<File> files;
    QStringList warnings;
};
}
```

`fileId` 建议为 `SHA256(directoryKey + NUL + 原始文件名UTF8)`；改名后 ID 改变，锁定由内容/项目身份接续，不依赖 fileId。UI 只能提交已在后台快照中的 ID。

`directoryKey` 基于 **已存在、拒绝符号链接的 gamePath 的规范路径**，而不是可能尚不存在的 mods 路径。跨平台路径大小写按实际文件系统处理，不把所有 POSIX 路径小写。首版可保守地拒绝同目录中文件名大小写折叠冲突的写操作，读取仍展示。

`snapshotId` 对排序后的 `(fileName,size,sha256)`、实例 MC/loader、stateRevision 做确定性序列化再 SHA-256。单纯比较 mtime/size 不足以提交更新；提交前重新哈希所有本计划会修改的旧文件，并检查依赖相关目录清单。

### 2.2 请求与结果

继续使用 `features.execute("mods", action, args)`。每次请求携带由 UI 生成的 `requestId`；返回 `completed` 的 data 加上该值和 instanceId。切换实例后忽略旧 requestId 的结果。

新增 `ModService::operationFailed(requestId, action, code, message)`，由 ServiceHub 转发为同签名的 `modOperationFailed`，供专用 UI 关联错误；同时保留 FeatureService 的 `failed` 供通用面板使用。`Main.qml` 在模组弹窗打开时避免再叠一层全局错误弹窗。

错误码建议固定为 `InvalidInput / Busy / StaleSnapshot / Cancelled / Unsupported / Network / Integrity / Conflict / Io / RecoveryRequired`。错误必须说明下一步：刷新、退出游戏、重新选择候选或查看恢复点。

### 2.3 actions 合约

以下均为 **建议新增接口**，不要假定当前已经存在。action 名在实现和测试中保持一致。

| action | args（省略每次都有的 requestId） | 返回/效果 |
| --- | --- | --- |
| `mods.scan` | `{instanceId}` | `{instanceId,snapshotId,items,warnings,directoryKey}` |
| `mods.duplicates` | `{instanceIds}` | 内容重复、同 modId 多版本、跨目录位置；只读 |
| `mods.enabled` | `{instanceId,snapshotId,fileIds,enabled}` | 全批事务；成功刷新扫描，不做隐式状态翻转 |
| `mods.lock` | `{instanceId,snapshotId,fileIds,locked}` | 原子保存版本锁，递增 stateRevision |
| `mods.remove` | `{instanceId,snapshotId,fileIds,confirm:true}` | 创建恢复点后移出 mods；不是直接永久删除 |
| `mods.backups` | `{instanceId}` | 按目录身份列恢复点与可恢复范围 |
| `mods.restore.plan` | `{instanceId,backupId}` | `{planId,changes,conflicts,canApply}` |
| `mods.restore.apply` | `{planId,confirm:true}` | 保护当前状态后恢复旧文件与相关锁状态 |
| `mods.update.plan` | `{instanceId,snapshotId,fileIds}` | 有候选/最新/无法识别/锁定/不兼容及候选 ID |
| `mods.dependencies.plan` | `{instanceId,snapshotId,fileIds}` | 解析所选模组必需依赖，返回候选与阻塞原因 |
| `mods.versions` | `{instanceId,snapshotId,fileId}` | 兼容版本候选，含 candidateId，不以版本字符串作 ID |
| `mods.version.plan` | `{instanceId,snapshotId,fileId,candidateId}` | 版本切换及前置的变化预览 |
| `mods.plan.select` | `{planId,selectedItemIds}` | 重新计算被选主项的依赖闭包，返回新 planId |
| `mods.plan.apply` | `{planId,confirm:true}` | 创建 taskId，严格执行后台保存的完整计划 |
| `mods.pause/resume/cancel/retry` | `{taskId}` | 只操作对应 mods 任务；过期重试要求重新规划 |

锁定语义：**阻止自动更新与自动版本替换**；不禁止用户明确启停。锁定模组手动换版本，先解锁再重新规划；不做隐藏的 `force=true` 绕过入口。移除锁定模组在确认页明确列出，用户确认后仍可移除，并保留恢复点。

### 2.4 任务状态与旧下载页兼容

ModService 本批同时只允许 **一个写任务**，不趁机重构全局下载队列。扫描可取消/重启；网络计划可有多个请求，但每个都绑定实例/快照/requestId。

`downloadState()` 兼容旧字段，并增加：

```json
{
  "module": "mods",
  "prefix": "mods",
  "taskId": "uuid",
  "instanceId": "uuid",
  "title": "更新 3 个模组",
  "phase": "downloading",
  "active": true,
  "paused": false,
  "completedFiles": 0,
  "totalFiles": 3,
  "bytes": 0,
  "totalBytes": 0,
  "name": "example.jar",
  "canPause": true,
  "canCancel": true,
  "canRetry": false,
  "error": ""
}
```

状态流：`planning → downloading → verifying → protecting → committing → complete`。异常到 `error`；提交失败先 `rollingBack`，恢复受阻到 `recoveryRequired`。用户取消在提交前进入 `cancelled`；提交关键段禁用取消，结束后再刷新结果。

DownloadsPage 按 phase 决定完成：下载文件数全满仍可能处于 verifying/protecting/committing，**不能在这时显示完成**。暂停只允许下载阶段；canPause/canCancel 按后端结果显示。按钮参数带 taskId；旧模块没有 taskId 时保持原 `{}` 路由。

## 3. B1：只读扫描和详情（任务 1–4）

### 任务 1：有边界的 JAR 读取和元数据解析（F18）

**文件：**新增 `ModTypes.h`、`ModMetadata.h/.cpp`、`tests/TestModFixtures.h`、`tests/tst_mods.cpp`；修改 CMake 与第三方声明。

**接口：**`Mods::File ModMetadata::read(const QString &path)`；单个损坏文件通过返回的 error 表示，不终止整个扫描。read 不负责写缓存、目录扫描或联网。

- [ ] 先写真实 ZIP/JAR 夹具。使用 libarchive 在 QTemporaryDir 中创建条目；不得用普通文本文件冒充 JAR 测试元数据成功。
- [ ] 新增 `readsFabricMetadata`、`readsQuiltMetadata`、`readsForgeMultiModToml`、`readsNeoForgeToml`、`readsLegacyMcmodInfo` 测试；首次在解析未实现时应失败。
- [ ] 读取白名单条目，其他内容跳过。限制元数据单条 2 MiB、图标压缩后读取 4 MiB、元数据总读取 16 MiB、最多遍历 100000 条目；拒绝链接、重复元数据条目和路径穿越。读取过程中也计数，不能只信 ZIP 声明大小。
- [ ] 加入 toml++ v3.4.0 的单头文件与许可证，保存在 `third_party/tomlplusplus/`；记录下载来源与实际 SHA-256，禁止填一个未验证摘要。构建不在每次配置时联网取 main 分支。
- [ ] 实现以下格式矩阵，保留 descriptor 多项与不能解析的依赖表达式。
- [ ] 添加损坏 ZIP、未识别 JAR、恶意图标路径、超限条目、多 descriptor 测试。图标解码前检查尺寸，首版可只显示通用图标，不能因图标不可用丢掉整个模组。
- [ ] 执行 `cmake --build build --target tst_mods -j4` 和 `./build/tst_mods`，通过后进入任务 2。

| 格式 | 读取位置 | 关键处理 |
| --- | --- | --- |
| Fabric | `fabric.mod.json` | id/name/version/authors；depends/recommends/suggests/breaks/conflicts；数组/对象保留逻辑 |
| Quilt | `quilt.mod.json` | `quilt_loader.id/version/metadata`；depends/breaks 的条件和 any/all 结构，不当 Fabric 原样解析 |
| Forge | `META-INF/mods.toml` | 遍历 `[[mods]]`；每个 `[[dependencies.<modId>]]` 关联正确 descriptor；mandatory/side/versionRange |
| NeoForge | `META-INF/neoforge.mods.toml`，兼容其明确支持的 legacy 入口 | 遍历 mods；识别 type/side 等字段，不能把 optional/incompatible 都当 required |
| Legacy Forge | `mcmod.info` | 数组或有 modList 的对象；缺少真实版本约束时标 unknown |
| 动态占位 | metadata 的版本占位符 | 仅解析有证据的 manifest/profile 映射；不能把 `${file.jarVersion}` 当真实版本参与排序 |

**执行时可直接采用的测试夹具 helper：**放到 `tests/TestModFixtures.h`，由调用测试使用 `QVERIFY(writeJar(...))` 检查结果。

```cpp
#pragma once
#include <QFile>
#include <QList>
#include <QPair>
#include <QString>
#include <QByteArray>
#include <archive.h>
#include <archive_entry.h>

inline bool writeJar(const QString &path,
                     const QList<QPair<QString,QByteArray>> &entries) {
    archive *out = archive_write_new();
    if (!out) return false;
    bool ok = archive_write_set_format_zip(out) == ARCHIVE_OK;
    if (ok) ok = archive_write_open_filename(out, QFile::encodeName(path).constData()) == ARCHIVE_OK;
    if (ok) {
        for (const auto &item : entries) {
            archive_entry *entry = archive_entry_new();
            if (!entry) { ok = false; break; }
            const QByteArray name = item.first.toUtf8();
            archive_entry_set_pathname(entry, name.constData());
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, item.second.size());
            ok = archive_write_header(out, entry) == ARCHIVE_OK;
            if (ok && !item.second.isEmpty()) {
                ok = archive_write_data(out, item.second.constData(), item.second.size()) == item.second.size();
            }
            archive_entry_free(entry);
            if (!ok) break;
        }
        const bool closed = archive_write_close(out) == ARCHIVE_OK;
        ok = ok && closed;
    }
    const bool freed = archive_write_free(out) == ARCHIVE_OK;
    return ok && freed;
}
```

**最小红/绿测试：**下列测试类是 `tst_mods.cpp` 的起点，之后继续加入测试槽。

```cpp
#include <QtTest>
#include <QTemporaryDir>
#include "TestModFixtures.h"
#include "ModMetadata.h"

class ModsTest : public QObject {
    Q_OBJECT
private slots:
    void readsFabricMetadata() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = temp.filePath("demo.jar");
        const QByteArray meta = R"({"schemaVersion":1,"id":"demo","version":"1.2.0","name":"Demo","depends":{"minecraft":">=1.20 <1.21","fabricloader":">=0.15.0"}})";
        QVERIFY(writeJar(path, {{"fabric.mod.json", meta}}));
        const auto file = ModMetadata::read(path);
        QVERIFY2(file.error.isEmpty(), qPrintable(file.error));
        QCOMPARE(file.descriptors.size(), 1);
        QCOMPARE(file.descriptors.first().id, QString("demo"));
        QCOMPARE(file.descriptors.first().version, QString("1.2.0"));
        QCOMPARE(file.descriptors.first().dependencies.size(), 2);
    }
    void rejectsBrokenJar() {
        QTemporaryDir temp;
        QFile out(temp.filePath("broken.jar"));
        QVERIFY(out.open(QIODevice::WriteOnly));
        QCOMPARE(out.write("not a zip"), qint64(9));
        out.close();
        QVERIFY(!ModMetadata::read(out.fileName()).error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ModsTest)
#include "tst_mods.moc"
```

### 任务 2：版本范围、目录快照和重复分组（F18、F20）

**文件：**`ModVersionRange.h/.cpp`、`ModCatalog.h/.cpp`、`ModService.h/.cpp`、`tests/tst_mods.cpp`。

**接口：**

```cpp
// ModVersionRange.h
Mods::Match matchModVersion(const QString &version,
                           const QString &expression,
                           const QString &dialect);

// ModCatalog.h：只在工作线程调用；取消时抛明确 Cancelled 结果，由服务捕获。
Mods::Snapshot scanMods(const QString &dataRoot, const QString &instanceId,
                        const std::atomic_bool &cancelled);
```

- [ ] 先测试范围语义：Fabric 的精确值、`>=`/`<` 合取、通配符、OR；Maven `[1.20,1.21)`、单点 `[1.20.1]`、开闭端点；Quilt 条件逻辑分别从参考项目夹具取证。未知表达式返回 Unknown，不返回 Yes。
- [ ] 不把 `1.20.10`、`1.20.9` 转 double，不把 Fabric/Forge/Quilt 所有范围压成一种 SemVer；保留预发布规则与版本方言。无法支持的表达式列入 UI warnings。
- [ ] `scanMods` 经 InstanceService 读取实例 MC/loader/gamePath，扫描单层 `.jar` 与 `.jar.disabled`；拒绝链接/特殊文件。不存在 mods 返回空列表，不 `mkpath`。
- [ ] 使用一遍流式读取同时计算 SHA-1、SHA-256；读取前后核对大小和修改时间，变化时重试一次，仍变化返回该项“文件变化中”。缓存可按路径/mtime/size 加速阅读，但变更计划提交前必须重算内容摘要。
- [ ] file row 保存全部 descriptor；嵌套 jar-in-jar 若本轮不能准确解析，为文件标注 `embeddedDependenciesUnknown`。后续自动补前置必须阻止可能重复的未知情况，不能直接下载一份。
- [ ] 构造三类重复报告：同 hash 的物理重复；同 modId 的不同文件（包括禁用状态）；跨实例出现位置。相同物理目录只扫描一次，多实例引用作为别名展示；同一 JAR 中重复声明单独报元数据警告。
- [ ] 用 `QThreadPool` / `QRunnable` 执行纯值扫描；完成后 queued invoke 回 ModService。服务销毁/取消用共享取消标记与 QPointer 防止访问已释放 QObject。
- [ ] `mods.scan` 回包带 requestId；切换实例后旧扫描完成不能覆盖新列表。

**必须加入的测试数据：**

| 测试名 | 输入 | 期望 |
| --- | --- | --- |
| `missingModsDirectoryStaysMissing` | 实例 game 存在、mods 不存在 | 扫描空列表，磁盘仍无 mods |
| `multiDescriptorIsOnePhysicalRow` | 一个 Forge JAR 声明两个 modId | 一行文件、两个 descriptor |
| `duplicateGroupsDistinguishContentAndIdentity` | A/B 相同 hash；C 与 A 同 modId 但不同版本 | 两种重复组分开，不能直接建议全部删 |
| `sharedDirectoryScannedOnce` | 两个实例解析到同一 gamePath | 一次物理扫描，报告两个引用实例 |
| `staleScanIgnored` | A 慢扫描、切 B 快扫描 | UI 最后显示 B；A 不覆盖 |
| `unknownRangeIsNotCompatible` | 无法解析的表达式/动态版本 | Unknown |
| `cancelDoesNotWriteUserFiles` | 大目录扫描中取消 | 无用户文件变化，停止后台任务 |

**交付点：**`mods.scan` 和 `mods.duplicates` 真正返回文件与元数据；不开放任何写 action。

### 任务 3：生产 QML 模组详情弹窗（F18）

**文件：**新增 `ui/ModsDialog.qml`；修改 `Main.qml`、`InstanceSettingsDialog.qml`、`ServiceHub.*`、CMake、`tests/tst_ui.cpp`。

- [ ] ServiceHub 注册 `mods`，增加 `modOperationFailed` 转发。保持旧模块 ID/action 不变。
- [ ] Main 增加 `openMods(instance)`，实例设置标题区域增加“管理模组”按钮；打开时保存目标 UUID，不通过后来变化的 `launcher.currentInstance` 决定写入目标。
- [ ] 弹窗布局：顶部实例名/MC/loader/共享目录说明；搜索与启用状态过滤；左侧文件列表，右侧元数据和依赖详情；底部刷新与关闭。小窗口下详情区滚动，长版本号/路径可展开或复制。
- [ ] 列表至少显示名称、版本、加载器、启用状态、兼容性、警告数量；多 descriptor 显示“包含 N 个模组”，详情逐项展开。
- [ ] 使用明确状态 `loading / ready / empty / error`，错误保留刷新入口；一个坏 JAR 显示独立错误行，不能清空全部列表。
- [ ] 重复报告作为页签/子视图只读展示。B1 不渲染写按钮，不用禁用的假按钮占位。
- [ ] 搜索只过滤已扫描结果，不每个键击重读 JAR；首版 QVariantList 足够，5000 文件使用 ListView 虚拟化，不用 Repeater 同时创建所有行。

**固定 objectName，方便真实 UI 测试：**`openModsButton`、`modsDialog`、`modsSearch`、`modsStateFilter`、`modsRefresh`、`modsList`、`modsDetails`、`modsError`、`modsDuplicateTab`。行使用 `modsRow_<fileId>`，不要用可变 index 作为长期选择身份。

**UI 验收：**从真实实例设置点击打开；读到测试 JAR 名称而非仅文件名；搜索后详情对应正确 fileId；坏 JAR 可见；`1060×720` 下依赖列表和关闭可达；下拉折叠/展开、悬停/选中颜色继承 GlassComboBox。

### 任务 4：B1 独立交付检查

- [ ] 构建、`tst_mods`、UI 测试、现有全部 CTest 通过。
- [ ] 使用几份实际来源的 Fabric/Forge/NeoForge/Quilt JAR 只读扫描，核对名称、版本、依赖；保存来源、版本和结果，不把下载来的整个模组包提交到源码仓库。
- [ ] 保存模组列表、详情、坏包、重复报告、小窗口截图到 `build/ui-verified/`。
- [ ] 在 `docs/migration/BATCH-B-MODS.md` 记录 B1 已完成及未知范围；F19/F21/F22/F23/F30 仍标未交付。
- [ ] B1 可交付后再开始 B2；不要因 B3 未完成把 B1 写成“模组管理全部完成”。

## 4. B2：目录保护、批量管理、恢复点（任务 5–8）

### 任务 5：统一目录租约，覆盖启动与旧入口（基础）

**文件：**新增 `ModDirectoryGuard.h/.cpp`；修改 GameService、ResourceService、CommunityService、InstanceService、BridgeService、ServiceHub 与相关测试构造方式。

**建议接口：**

```cpp
enum class ModLeaseKind { Game, Write };
struct ModLeaseResult { QString token; QString error; };

// Guard 由 ServiceHub 持有，所有服务注入同一个实例。
ModLeaseResult acquire(const QString &canonicalGameDir,
                       const QString &ownerId, ModLeaseKind kind);
void release(const QString &token);
bool blocked(const QString &canonicalGameDir) const;
```

- [ ] 先写 Guard 测试：A/B 两个实例共享目录；A 持游戏租约时 B 写失败；两个独立目录不冲突；同目录两个写请求互斥；释放后可申请。
- [ ] 目录租约在进程内用 mutex 保护；用启动器数据目录下按 directoryKey 命名的 QLockFile 防另一个 XMCL 进程竞争。不要把锁文件写进不存在的 mods，避免扫描副作用。
- [ ] GameService 在启动准备会影响活动目录之前申请 Game 租约，至少在 `QProcess::start()` 之前持有；从 Starting 到游戏实际退出一直持有。FailedToStart 释放；运行期 error 信号不能提前释放，须核实进程已经 NotRunning。
- [ ] 写任务准备/下载阶段不持写租约，提交前申请并重新验证快照；申请失败保留已下载文件，告知退出游戏后重试规划。修改锁状态也需要短写租约，避免计划与锁记录不同步。
- [ ] 原有 `resource.import/toggle/remove` 对 mods 接入同一写保护；CommunityService 安装 mods 改为 staging，提交持租约；Bridge 安装 JAR 同样处理。
- [ ] InstanceService 的删除、隔离切换、改目标目录和影响 mods 的恢复操作接入保护；复制/备份至少要求稳定源快照，运行/提交时给出忙提示。PackService 写全新不可见 staging 可继续，发布前确认无同目录复用。
- [ ] 检查其他可写 mods 的入口，不只在新弹窗禁用按钮。Standalone service 测试也注入 Guard；未注入时新写路径拒绝执行，不静默绕过保护。
- [ ] 应用关闭但游戏继续运行时，不能假设 QLockFile 自动清理后目录空闲。持久化自有游戏 PID/启动时间/目录，下一次运行核对存活与进程身份；无法可靠确认就标“运行状态待确认”，保守禁止自动写入。不可结束其他程序的 Java。

**验收：**模拟慢 QProcess 启动，Starting 阶段写入也被拒绝；旧 community/resource/Bridge 入口不能绕过；同目录另一 XMCL 进程拒绝写；不同目录允许只读扫描。

### 任务 6：锁定记录与后台计划身份（F19）

**文件：**`ModStateStore.h/.cpp`、ModService、ModCatalog、`tests/tst_mods.cpp`。

**存储：**`<dataRoot>/mod-state/<directoryKey>.json`，schemaVersion=1。目录身份分开，使用 QSaveFile 保存。

```json
{
  "schemaVersion": 1,
  "revision": 3,
  "aliases": {"sha1:0123456789012345678901234567890123456789": "modrinth:project-id"},
  "locks": {"modrinth:project-id": true},
  "sources": {
    "sha1:0123456789012345678901234567890123456789": {
      "provider": "modrinth", "projectId": "project-id", "versionId": "version-id"
    }
  }
}
```

上面的哈希仅是格式示例；正式记录必须来自真实内容。`sources` 中不能存 API Key。

- [ ] 无来源时 identity=`sha1:<hash>`；确认远端文件 hash 相同后才升级到 `provider:projectId`。不能凭模组名称自动绑定项目。
- [ ] 批量锁定验证 fileIds 全部属于当前 snapshot、文件仍存在且 hash 一致；一次原子写入，任何非法项都不部分保存。
- [ ] 标记版本锁只影响自动更新/替换；手动启停允许。锁定记录丢失/损坏需报错，不能 catch 后自动当成全部未锁。
- [ ] 文件重命名后内容不变，应能找到原锁；成功版本替换后绑定新 hash 并保留项目身份；解锁项目同时清理相关旧 hash 锁，避免重扫又锁回来。
- [ ] 每次状态更新增加 revision，旧更新计划全部失效；QML 操作成功后刷新当前快照。

**测试：**`lockSurvivesRename`、`lockIsDirectoryScoped`、`unlockClearsAliases`、`corruptStateDoesNotBecomeUnlocked`、`staleSnapshotCannotLock`、`batchLockIsAtomic`。期望分别覆盖重命名、不同目录、重复别名、坏 JSON、过期快照、批次错误。

### 任务 7：文件事务、持久化 journal 与可恢复备份（F30）

**文件：**`ModFileTransaction.h/.cpp`、`tests/tst_mod_transactions.cpp`。这一步先用临时目录模拟文件，不依赖公网。

**事务条目约定：**每项描述一个物理文件变化，而不是一个 descriptor。

```cpp
struct ModFileChange {
    QString oldName;           // 新增时为空
    QByteArray oldSha256;      // oldName 非空时必须提供
    QString newName;           // 移除时为空
    QString stagedFile;        // 新增/替换源，必须位于本任务 staging
    QByteArray newSha256;      // newName 非空时必须提供
};
struct ModTransactionResult {
    bool ok = false;
    QString backupId;
    QString error;
    bool recoveryRequired = false;
};
```

`ModFileTransaction::apply(gameDir, directoryKey, changes, expectedStateRevision, cancelled)` 只接受服务内部构造的数据，UI 不可传 stagedFile/目标路径。调用者必须持写租约。事务类从构造注入 dataRoot 和状态存储；使用当前线程同步文件 IO，但由工作线程调用。

**布局：**

```text
<gameDir>/.xmcl-mod-tx/<transactionId>/
  journal.json
  new/                       已下载并验证的新文件
  old/                       已验证的旧文件保护副本
  state-before.json
  state-after.json
<dataRoot>/mod-backups/<directoryKey>/<backupId>/
  manifest.json
  files/
  state.json
```

两处可能跨盘，不能先删旧文件再跨盘 rename。正式写之前完成保护副本复制和 hash 复核；同盘 staging 用于局部提交。备份转存失败时停止提交或保留 staging 作为明确的恢复位置，不能“继续但没有备份”。

- [ ] 写失败注入器（测试配置/依赖注入），可在保护文件 N、移除旧项 N、写入新项 N、状态写入、journal 写入、恢复旧项 N 精确失败。不要依赖 chmod 在所有平台都能稳定模拟错误。
- [ ] **验证：**文件名限定单层 jar/jar.disabled；拒绝重复源/目标、大小写冲突、链接、过期状态、目标外部变化。旧 hash 必须重新计算，不能只比文件大小。
- [ ] **准备：**所有新内容已下载并验证；游戏目录里的“new”不被游戏 mods 扫描；磁盘不足在改旧文件前报告。
- [ ] **保护：**逐项复制旧文件、hash 复核；保存状态快照；原文件不动。journal 用 QSaveFile 持久化。
- [ ] **提交：**写操作意图到 journal 后再执行；记录每项完成状态。新目标用不覆盖方式创建；提交前检查的时间窗口内出现外部同名文件应失败，不能让 QSaveFile 默认覆盖它。
- [ ] **一致性：**文件和 ModStateStore 是同一个逻辑事务。状态写失败也回滚文件；启动恢复同时比较状态 revision 和文件 hash。
- [ ] **回滚：**只删除本事务创建且 hash 仍等于 newSha256 的文件；若内容被外部改动则停止自动覆盖，保留全部恢复材料并报 recoveryRequired。恢复旧文件同样不覆盖未知新内容。
- [ ] **启动恢复：**启动器允许游戏启动/写操作前扫描未完成 journal；按磁盘内容+已写意图判断阶段，而非仅信“completed”标记；缺 journal 的未知目录保留并显示问题，不自动清空。
- [ ] **结束：**成功保存可见恢复点；失败但完整回滚显示“失败，已恢复”；恢复受阻显示路径与处理入口，不显示普通成功/已取消。

**journal 最小字段：**schemaVersion、transactionId、directoryKey、instanceId、operation、phase、createdAt、sourceSnapshotId、stateRevisionBefore/After、每项 oldName/newName/oldHash/newHash/保护副本位置/intent/完成状态。

**幂等恢复判断表：**

| 磁盘情况 | 恢复处理 |
| --- | --- |
| 旧文件仍是原 hash，新目标不存在 | 无需恢复该旧项 |
| 旧文件不在原位，保护副本 hash 正确 | 无冲突时恢复原位 |
| 新目标 hash 正是事务的新 hash | 可移除本事务新项，再恢复旧项 |
| 新目标为第三种 hash | 不删除、不覆盖，RecoveryRequired |
| 备份缺失/摘要不符 | 停止自动恢复，保留其余材料 |
| journal 说未完成但文件已经是新 hash | 结合 intent、备份与状态判断，重跑不重复覆盖 |

**最低测试清单（每项比较最终磁盘内容，不只测 bool）：**

1. 替换成功，旧内容可在恢复点读取。
2. 第二个新文件写失败，两个旧文件均复原。
3. 状态保存失败，文件与锁状态均恢复。
4. 外部程序修改新文件，回滚不删除它，恢复点仍完整。
5. 保护副本失败，没有任何活动文件变化。
6. 逐阶段模拟进程中断后重新构造事务服务，恢复可重复执行。
7. 重复源/目标、穿越、链接、大小写冲突均被拒绝。
8. 下载已完成但旧文件 hash 改变，停止提交。
9. 取消在提交前不改活动文件；进入关键提交段则禁用取消并完成确定结果。

进程崩溃恢复与突然断电耐久性分开描述。QSaveFile 不是整目录事务，也不能单凭本机单元测试宣称所有文件系统断电绝对安全。

### 任务 8：接通批量操作与恢复点 UI（F19、F20、F30）

**文件：**ModService、ModFileTransaction、ModsDialog、ServiceHub、`tests/tst_ui.cpp`。

- [ ] 启用/禁用接受目标布尔状态。已处于目标状态视作 no-op；其余项生成事务。目标存在不同内容则整批拒绝，并列出冲突项；不要处理了一半才发现输入无效。
- [ ] 一个物理 JAR 只提交一次。选中多个 descriptor 时去重为 fileId；启停保留锁定身份。
- [ ] 重复清理进入确认页：列出每个物理文件、依赖影响、保留项。跨实例重复仅展示，默认不提供“一键全删”；启用文件与禁用文件分别标识。
- [ ] 移除使用保护+移出活动目录，恢复点可找回；不直接调用 QFile::remove 永久丢弃原件。
- [ ] 增加“恢复点”页签，列标题、操作时间、文件数量、来源实例/共享目录、状态。恢复先 plan；默认只改备份涉及的路径，其余目录文件保持。
- [ ] 恢复前保护当前状态，形成新的可逆操作；发现后来被玩家改的文件必须展示冲突并阻止自动覆盖。
- [ ] 首版自动备份不设不可见后台删除。默认最多保留 20 个可清理自动恢复点或 2 GiB（先触及者提示清理），手工标记保留的恢复点不参与；超过阈值先提示管理，不因阈值自动删唯一可恢复旧文件。未完成事务不计入可清理集合。
- [ ] 统一 `modsTaskStatus`、`modsEnableSelected`、`modsDisableSelected`、`modsLockSelected`、`modsBackupList`、`modsRestorePreview`、`modsRestoreConfirm` objectName。
- [ ] 原 ResourceService 的 mods 修改转同一保护/事务 helper，保留它原来的 completed/failed action 名兼容旧 FeaturePanel；迁移测试夹具，不放宽生产校验。

**B2 UI 验收：**通过实例设置打开 → 选择两项禁用 → 目录出现 `.jar.disabled` → 重新扫描状态保持 → 锁定其中一项 → 查看恢复点 → 恢复 → 原 JAR 与锁状态符合预览。运行游戏时旧入口和新入口都拒绝写；共享目录另一实例的操作也拒绝。

## 5. B3：在线更新、前置补齐与版本切换（任务 9–12）

### 任务 9：可信来源识别与候选查询（F21、F23）

**文件：**新增 `ModRemoteClient.h/.cpp`、`tests/TestModServer.h`、`tests/tst_mod_remote.cpp`；扩展 ModStateStore/ModService。

**网络边界：**生产 HTTPS；测试通过构造参数注入 loopback baseUrl，不从普通 UI 接收任意 API 根地址。设置请求超时、响应大小限制、明确 User-Agent、取消 generation；凭据只在 C++。

- [ ] 优先实现 Modrinth 的内容哈希识别、更新和版本候选。按官方 API 规范及参考 KAMUCL 实现核对请求字段，保存本批使用的文档链接/接口夹具。
- [ ] `POST /v2/version_files`：通过 SHA-1 批量识别实际文件版本；`POST /v2/version_files/update`：带 hashes、algorithm、game_versions、loaders 查询候选；`GET /v2/project/<id>/version`：取可切换版本；固定依赖 version_id 用 `GET /v2/version/<id>`。
- [ ] 远端返回不直接信任：候选必须匹配 MC/loader，URL 合法、filename 单层、提供可验证摘要、大小合理；从版本 files 中选择正确主 JAR，不能把 source/dev JAR 自动当游戏文件。
- [ ] 以平台 versionId、发布时间和兼容集合决定候选，不用字符串排序判断版本新旧；候选 hash 与当前相同显示“当前已是此文件”。
- [ ] 来源识别失败显示“未识别，无法自动更新”，不能显示“已是最新版”。名称搜索只给建议，用户选项后仍需 hash/文件证据才绑定来源。
- [ ] CurseForge 复用当前 Key 存储。已知 projectId/fileId 的文件通过 API 查询候选/required 依赖；任意本地 JAR 暂无来源且本批未实现官方 fingerprint 识别时，明确显示不支持自动识别，不能用名称猜项目。该能力边界写入交付记录。
- [ ] HTTP 429 遵守 Retry-After 并可取消；401/403 提示 Key/权限；无 downloadUrl 返回“需手工获取”，本批不偷偷跳过必要文件宣布更新成功。

**统一候选字段：**candidateId（后台临时 ID）、provider、projectId、versionId、name、versionLabel、publishedAt、minecraftVersions、loaders、filename、downloadUrl、sha1/sha512、size、dependencies。公开 UI DTO 可隐藏 downloadUrl/hash 的细节，服务保存完整候选。

**网络测试：**返回错 loader/MC、hash 相同、无摘要、非法 filename、慢旧请求、429、没有匹配结果、两个文件一个 primary、Key 缺失、取消后迟到响应。所有失败都不能写活动目录。

### 任务 10：依赖图与有期限的变更计划（F21、F22、F23）

**文件：**`ModPlanner.h/.cpp`、ModService、ModRemoteClient、ModVersionRange、`tests/tst_mod_remote.cpp`。

- [ ] 输入为不可变 Snapshot、用户选择的主候选、客户端 MC/loader/runtime 信息；输出只读 plan。planId 随机、有效期 15 分钟，保存于后台内存，应用重启不直接执行旧计划。
- [ ] 计划保存 directoryKey、instanceId、sourceSnapshotId、stateRevision、MC/loader、所有旧 hash、目标候选、warnings/blockers；UI 只提交 planId 与选择，不能改目标 URL/name/path/hash。
- [ ] 建立本地启用 descriptor/provides 索引。minecraft、java、fabricloader、forge、neoforge、quilt_loader 等平台能力按实际安装版本加入，不能作为普通模组随意下载；缺真实版本时输出 unknown。
- [ ] required 与 optional/incompatible 分开；server-only 的前置在明确客户端语义下排除。固定 versionId 必须选该版本，不因“更新”自动换成最新。
- [ ] 项目依赖闭包：visited 键为 provider+projectId+versionId；设置节点 256、深度 32 上限。遇到循环停止递归，但对已选节点继续校验所有约束；不是见到循环就自动认为满足。
- [ ] 本地 modId 不能直接等于平台 projectId。没有可信来源映射的 required modId，只展示缺失并引导搜索/明确选择；自动补齐仅对确定身份且满足约束的候选开放。
- [ ] 优先满足已安装且启用的依赖；已禁用依赖不视为有效。已安装不满足、候选互斥、锁定前置需要变更、嵌套依赖无法判断都生成 blocker，首版停止自动应用，不自行卸载/解锁/启用其他模组。
- [ ] 初始候选来自 required 解析；全部文件下载后再解析实际 JAR 元数据，复核每个 descriptor 的 loader/版本/前置。远端标签一致但内部不兼容应停止提交。
- [ ] `mods.plan.select` 必须重新算依赖闭包并生成新 planId。取消某个主模组后，不再需要的前置从计划移除；同时被两个主模组使用的前置只安装一次。
- [ ] 更新、前置补齐、手动版本切换统一输出 ModFileChange 计划；版本切换可以明确选择降级，但不能绕过 lock 或兼容性。手动版本切换不改变 Minecraft 版本。

**计划处理流程（算法约定）：**

```text
读取后台快照和用户选择
  → 验证 fileId/candidateId 属于本请求与实例
  → 筛选 MC/loader/客户端候选
  → 创建“主变更 + 本地启用模组”的目标集合
  → 递归补必要平台依赖，累计全部约束
  → 比较依赖版本/冲突/锁定/来源可验证性
  → 对物理文件去重，检查目标名称碰撞
  → 若有 blocker，返回可查看但不可应用的计划
  → 否则保存 15 分钟有效计划并供 UI 确认
```

**测试图必须明确：**

| 场景 | 期望 |
| --- | --- |
| A、B 都依赖 C | 只装一个兼容 C |
| A→B→A | 有界终止，约束仍检查 |
| A 固定依赖 B-v1，B 最新 v2 | 选 v1，不偷换 v2 |
| A 要 B>=2，C 要 B<2 | blocker，活动目录不改 |
| B 已安装但禁用 | 不视为满足，提示用户处理 |
| 必需 B 是锁定旧版 | blocker，不自动解锁 |
| 本地 modId 相同但不知来源 | 不凭名称下载陌生项目 |
| 原候选选中后 MC/loader/锁 revision 变化 | StaleSnapshot，重新规划 |
| 同一个 JAR 提供 B 和 C | 一个物理文件，不重复下载/替换 |

### 任务 11：计划应用、文件校验和下载页（F21、F22、F23、F30）

**文件：**ModService、ModPlanner、ModFileTransaction、ServiceHub、DownloadsPage、`tests/tst_mod_remote.cpp`、`tst_mod_transactions.cpp`。

- [ ] 确认有效 plan 后创建 taskId，保存原计划与源目录身份。网络下载放入 `<gameDir>/.xmcl-mod-tx/<taskId>/new/`；DownloadEngine 的 destination 必须是 staging。
- [ ] 先下载所有所选主项及其必需依赖，经 API 摘要验证，再做任务 10 的实际 JAR 元数据复核。全部准备好再申请写租约。
- [ ] 提交前重新读取实例 gamePath/MC/loader、mods 文件集合、受影响旧 hash、锁 revision；任一变化则不提交，返回需要重新预览。
- [ ] 下载成功不等于安装成功；调用统一事务 apply，备份成功、文件和状态一致后才发 complete。一个用户确认的计划整体提交或恢复，不做未说明的“部分成功”。
- [ ] 保存新版本 source/hash/identity，保持原 `.disabled` 后缀和项目锁关系；新前置根据计划明确是启用还是待用户处理。
- [ ] 下载/准备阶段取消停止网络、清除重试定时器、使 generation 失效；不要删除恢复点。失败重试重新验证计划和快照，过期则回预览，不机械重放旧 URL。
- [ ] ServiceHub 聚合 mods task，DownloadsPage 按 taskId 路由。保留游戏/社区/整合包原入口，并修正“文件数满就完成”的通用判断，使 mods 校验/提交阶段仍显示处理中。
- [ ] 页面关闭只关闭 UI，不自动取消正在执行的后台任务；再次打开显示状态。切换实例不会把 A 的任务状态写成 B 的列表。

**验收：**本机 HTTP 服务实际下载主 JAR 和前置；提交后一一核对内容 hash；失败、暂停/继续、取消和过期均按真实状态显示；旧文件在任何网络错误期间仍可读取。

### 任务 12：更新预览、版本选择与完整 UI 流程（F21–F23）

**文件：**`ui/ModChangeDialog.qml`、ModsDialog、Main、CMake、`tests/tst_ui.cpp`。

- [ ] ModsDialog 增加“检查更新”“补齐前置”“切换版本”；仅对有意义的选择启用。版本锁直接显示原因和解锁入口。
- [ ] ModChangeDialog 显示旧/新版本、主文件/前置、下载大小、保留禁用状态、备份说明、blockers；用 GlassComboBox 选择版本，不让用户填写 versionId。
- [ ] 主项勾选变化调用 `mods.plan.select`，重新规划期间禁用确认；blocker 不得被普通确认复选框绕过。
- [ ] 应用后进入可见任务进度，失败保留“查看原因/重新预览/恢复点”；成功刷新原实例扫描。所有 UI 结果按 requestId、instanceId、planId 匹配。
- [ ] 固定 objectName：`modsCheckUpdates`、`modsFixDependencies`、`modsChooseVersion`、`modChangeDialog`、`modChangeList`、`modChangeBlockers`、`modChangeApply`、`modVersionChoice`。
- [ ] 最小窗口、键盘 Tab/Escape、空列表、断网、长中文文件名、慢请求期间换实例各验证一次。

**生产 QML 必须通过的四条路径：**

1. 现有实例设置 → 管理模组 → 扫描实际 fixture JAR → 查看详情 → 搜索/重复报告。
2. 选两项 → 禁用 → 锁定一项 → 关闭重开 → 状态保持 → 恢复点预览与恢复。
3. 检查更新 → 看到旧/新版本和必需前置 → 确认 → 本机 HTTP 真下载 → 校验后提交 → 文件正确。
4. 手动选择兼容旧版本 → 预览降级 → 模拟第二个文件提交失败 → UI 显示已回滚 → 原文件和状态完全一致。

## 6. CMake 与验证命令

### 6.1 CMake 增量

新测试文件存在后，在当前 foreach suite 列表追加 `mods`、`mod_transactions`、`mod_remote`。当前文件列表已包含 `java`，不要用旧总览的 12 套件配置覆盖它。

```cmake
# B1 引入；toml.hpp 与 LICENSE 均来自已经核对的 v3.4.0。
target_include_directories(xmcl_services PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus")

# B1 在现有 QML_FILES 后追加；B3 创建 ModChangeDialog 时再追加第二项。
list(APPEND QML_FILES ui/ModsDialog.qml)
# B3: list(APPEND QML_FILES ui/ModChangeDialog.qml)
```

QML_FILES 追加必须位于当前给 QML 设置 resource alias 的 foreach **之前**。测试命令参考：

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH='/opt/homebrew;/opt/homebrew/opt/libarchive' -DBUILD_TESTING=ON
cmake --build build -j4
ctest --test-dir build -R '^mods$' --output-on-failure
ctest --test-dir build -R '^mod_transactions$' --output-on-failure
ctest --test-dir build -R '^mod_remote$' --output-on-failure
ctest --test-dir build -R '^ui$' --output-on-failure
ctest --test-dir build --output-on-failure
```

未创建后续测试目标前不把对应命令的“找不到测试”视为通过。预期 B1 为原 13 + mods，B2 再加 transactions，B3 再加 remote；以执行时项目实际套件数为准。

### 6.2 每批测试顺序

1. 添加能够揭示缺失行为的测试，先确认失败原因确实是缺功能，不是环境坏。
2. 写最小实现，只跑相关目标直到通过。
3. 跑生产 UI 用例与全量 CTest，确认旧下载、Java、启动检查没退化。
4. 原生图形后端打开一次应用，检查玻璃/下拉/滚动；offscreen 测试不代替 Metal 等原生渲染检查。
5. B3 单独做真实 Modrinth 查询与可验证文件下载；用专用测试实例实际启动一次模组组合。记录所用 MC/loader/mod 版本，不承诺所有组合均兼容。

失败用例清单不得只写“处理异常”。必须覆盖：请求晚到、计划过期、文件外部变化、坏 hash、目标冲突、备份失败、状态保存失败、部分提交失败、回滚受阻、程序中断恢复、共享目录游戏运行、跨进程竞争。

## 7. 交付判定与记录模板

### B1 完成条件

- [ ] 五类元数据可以读；多 descriptor/未知/损坏包结果清晰。
- [ ] 用户从实例设置真实进入面板，不手填 UUID。
- [ ] 只读扫描不创建或改用户游戏文件。
- [ ] 同内容/同 modId/跨实例重复正确区分，无自动删除。
- [ ] UI 及旧功能回归通过，记录未支持的版本表达式/嵌套包情况。

### B2 完成条件

- [ ] 原入口和新入口都受目录保护；Starting/Running 期间不能修改活动 mods。
- [ ] 批量启停/锁定幂等，修改前备份真实存在。
- [ ] 恢复点可从界面预览和恢复，恢复本身也可逆。
- [ ] 故障注入证明原文件保全；崩溃重启能处理 journal；外部修改不被回滚覆盖。

### B3 完成条件

- [ ] 已识别来源的模组可生成真实更新/前置/版本切换计划。
- [ ] Unknown、locked、incompatible、missing-source 与最新状态不混淆。
- [ ] 依赖闭包符合所选主项，冲突不被自动覆盖。
- [ ] 真实下载、校验、元数据复核、事务提交和恢复连通。
- [ ] 生产 UI 四条路径通过，F23 兼容版本切换/降级未遗漏。
- [ ] CurseForge 任意 JAR 指纹识别、限制下载、复杂嵌套依赖等未覆盖边界如实记录。

每次交付在 `docs/migration/BATCH-B-MODS.md` 写：

```text
交付阶段：B1 / B2 / B3
实际入口：
本次实现的功能编号：
新增/修改文件：
自动测试命令与实际结果：
原生 UI 截图路径：
真实网络/游戏验证的版本与结果：
未实现或未验收的边界：
已知问题与下一阶段：
```

同步更新 `docs/migration/STATUS.md` 对应行，保留其他模块的未完成记录。不为“交付好看”把未跑的测试填为通过；没有 Git 仓库时不虚构提交哈希。

## 8. 直接发给 DeepSeek 的执行指令

```text
请执行 docs/superpowers/plans/2026-10-27-batch-b-mod-management.md。

先核对当前 src/、ui/、tests/，保留已经实现的批次 A、下载弹窗、
社区弹窗、实例设置与 GlassComboBox。保持主界面的森林背景、圆角、玻璃风格。

本轮先只交付 B1（任务 1–4）：五类模组元数据解析、兼容性三态、
真实后台扫描、文件/descriptor 分离、重复报告和生产 QML 详情面板。
不要提前加启停/更新假按钮，不修改用户的模组文件。

按计划定义的数据结构和 action 合约写代码；先写真实 ZIP/JAR 夹具测试，
确认失败，再实现并通过测试。完成后跑生产 UI 和全量 CTest，
原生打开应用检查最小窗口、下拉颜色和滚动。

输出：实际改动、入口、测试结果、截图与未支持边界，更新 BATCH-B-MODS.md。
后续 B2/B3 按同一计划继续；不扩展到 VoxLink、整合包升级或更新分发。
不要发布 Release，不推送 KAMUCL，不覆盖其他人的修改。
```

B1 验收后，下一轮把“本轮先只交付 B1（任务 1–4）”改成“本轮交付 B2（任务 5–8）”；B2 验收后再执行 B3（任务 9–12）。如果用户直接授权持续完成整批，可顺序推进，但仍保留每段验证和交付记录。

## 9. 源码与规范参考

- [KAMUCL 模组元数据](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modMetadata.ts)、[模组信息/重复检测](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modinfo.ts)。
- [批量管理、版本选择与前置计划](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modManagement.ts)、[身份与锁定](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modState.ts)。
- [哈希更新](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modUpdates.ts)、[文件事务](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modTransaction.ts)、[修改保护](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/changeProtection.ts)。
- [KAMUCL 兼容性测试](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/tests/mod-workflow.test.ts)、[管理事务测试](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/tests/workbench-mod-management-1054.test.ts)。
- [toml++ v3.4.0 官方说明](https://github.com/marzer/tomlplusplus/blob/v3.4.0/README.md)：支持 C++17，可使用单头形式；版本固定不代表必须升级现有全项目依赖。
- [Modrinth 官方 API 文档入口](https://docs.modrinth.com/api/)：实施在线步骤时核对字段与限流，不根据旧例子猜接口。

KAMUCL 本地参考目录为 `build/reference/KAMUCL/`，仅作参考。移植具体代码时保留对应许可证/归属；本计划给出的目录保护、journal 和状态接口是 XMCL 的实施设计，不宣称 KAMUCL 已按这些接口实现。
