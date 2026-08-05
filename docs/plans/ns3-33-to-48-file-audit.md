# ns-3.33 到 ns-3.48 最终逐文件审计

审计基线是 `legacy/ns-3.33` commit
`f4c7bff6674f9eeaae30e080d52c38c7fb601e21`。该版本
`contrib/satcompute` 的 322 个路径按字典序连接后的 SHA-256 为
`d6e4a248bbeeae1da5b9b2d71feca7a005619645cba5d8066eacf9a02d20664e`。
70 个 legacy-only 路径按同样规则得到的 SHA-256 为
`83be4f5013eae055225a982fdc0122070d6bfd34da2e70aacc2ebbe337e90153`。

阶段 7 收口后的结果：

- 252 个 legacy 路径在 main 原路径保留并按 ns-3.48 API 适配；
- 70 个 legacy-only 路径全部记录在
  [`ns3-33-to-48-file-audit.tsv`](ns3-33-to-48-file-audit.tsv)，没有空结论；
- main 的 `contrib/satcompute` 共 388 个文件，其中 136 个是 ns-3.48 或合同测试
  所需的 current-only 文件；它们按迁移矩阵归入配置、原生轨道、online、replay、
  export、稳定 IPv4、schema、测试和说明，不存在未归属的通用重构目录。

TSV 每行固定包含 legacy 路径、`replaced/removed` 动作、仍存在的证据路径和中文
理由。`test_legacy_file_audit.py` 会检查 70 行完整、唯一、排序、路径集合哈希，旧
路径没有回流，且所有证据文件仍存在。测试层的详细行为映射另见
[`ns3-33-to-48-test-audit.md`](ns3-33-to-48-test-audit.md)。

## 删除边界

70 个路径只属于以下已批准边界：

- `wscript` 由 ns-3.48 `CMakeLists.txt` 替代；
- 旧完整 scenario 配置由 `para.cc + constellation + 独立数据` 替代；
- Hypatia/TLE/Python 轨道传播由唯一 C++ 圆轨道核心替代；
- 旧 route-audit/冻结报告由生产 routing API 门禁和可重算 trace 分析替代；
- 旧 Hypatia PNG 属于生成物，当前 renderer 从 manifest XYZ 按需重建；
- 22 个测试路径均有当前单元或集成测试接替。

因此，没有删除 NetworkTransfer、任务、五种 IPv4 路由、metrics、诊断、输入
fixture 或可观察输出。`routing-summary.json` 与
`routing-reservation-events.csv` 是 ns-3.48 迁移中产生、后经字段审计删除的
过渡输出，不属于 legacy 合同。

## current-only 目录结论

阶段 7 同时移除了 current 的通用 `model/` 分类：`satcompute-version.*` 和
`sha256.*` 移到模块根目录，与 `effective-config.*`、`resolved-config.*` 一起
承担跨组件的可复现证据职责。其余 current-only 文件全部位于所属业务目录：

| 归属 | 保留原因 |
|---|---|
| 根目录 | CMake、para、resolved/effective config、版本与 SHA-256 证据 |
| `topology/orbit` | 星座 closed-world 读取与唯一原生圆轨道核心 |
| `topology/online` | 固定候选、距离门控、fixed/distance 时延和 network tick |
| `topology/replay` | 规则 cadence 的权威 manifest 回放 |
| `topology/export` | ECEF XYZ、活动 ISL、切片 schema 与哈希 manifest |
| `topology/ipv4` | 稳定 satellite ID 对应的 service `/32` 与 ISL `/30` |
| `traffic`、`task` | current 内部记录类型和独立 JSON schema |
| `tests` | ns-3.48 C++ executable、非法输入、online/export/replay 与审计门禁 |

故障执行、前后端实时传输、IPv6/SRv6 和非圆轨道仍是明确延期项，不以占位代码
伪装为已迁移能力。
