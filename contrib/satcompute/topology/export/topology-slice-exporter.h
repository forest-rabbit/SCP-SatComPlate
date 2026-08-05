/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_TOPOLOGY_SLICE_EXPORTER_H
#define SATCOMPUTE_TOPOLOGY_SLICE_EXPORTER_H

#include "../online/circular-orbit-topology-policy.h"
#include "../orbit/online-orbit-constellation.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace ns3
{

/** topology-only 切片无法生成或写出时抛出的异常。 */
class TopologySliceExporterError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** 一个采样时刻实际写出的节点和链路文件。 */
struct TopologySliceRecord
{
    int64_t simulationTimeNs{};       ///< 精确采样时刻。
    std::filesystem::path nodesPath;  ///< 节点坐标文件。
    std::filesystem::path linksPath;  ///< 全部固定候选链路文件。
    uint32_t activeLinkCount{};       ///< 该时刻有效的候选链路数量。
};

/** 一次 topology-only 运行的有序输出结果。 */
struct TopologySliceExportResult
{
    std::filesystem::path outputDirectory;   ///< 切片输出目录。
    std::vector<TopologySliceRecord> slices; ///< 按时间递增的切片。
};

/**
 * 生成 topology-only 的规范采样时刻。
 *
 * @param durationNs 仿真持续时间。
 * @param intervalNs 切片间隔。
 * @param includeFinalState 是否包含精确终点。
 * @return 从零开始、严格递增的整数纳秒采样时刻。
 */
std::vector<int64_t> BuildTopologySliceTimes(int64_t durationNs,
                                             int64_t intervalNs,
                                             bool includeFinalState);

/**
 * 将整数纳秒格式化为无多余尾零的十进制秒文件名片段。
 *
 * @param simulationTimeNs 非负仿真时刻。
 * @return 例如 `0`、`1` 或 `1.000000001`。
 */
std::string FormatTopologySliceTimeToken(int64_t simulationTimeNs);

/**
 * 按独立时间间隔写出原生轨道位置和全部固定候选链路状态。
 *
 * 本类不创建 InternetStack、NetDevice、路由、FlowMonitor、任务或指标对象。
 * 轨道和链路计算由调用方提供，因此与正式在线仿真共享同一实现。
 */
class TopologySliceExporter
{
  public:
    /**
     * 构造切片器。
     *
     * @param durationNs 仿真持续时间。
     * @param intervalNs 切片间隔。
     * @param includeFinalState 是否包含精确终点。
     * @param linkBandwidthBps 每条候选 ISL 的带宽。
     * @param outputDirectory `nodes_*` 和 `links_*` 输出目录。
     * @param constellation 已安装原生 mobility 的星座。
     * @param policy 共享的固定候选拓扑策略。
     */
    TopologySliceExporter(int64_t durationNs,
                          int64_t intervalNs,
                          bool includeFinalState,
                          uint64_t linkBandwidthBps,
                          const std::filesystem::path& outputDirectory,
                          const OnlineOrbitConstellation& constellation,
                          const CircularOrbitTopologyPolicy& policy);

    /** 在时刻零写出首个切片并调度后续切片。 */
    void Initialize();

    /**
     * 校验全部切片已经写出并返回结果。
     *
     * @return 有序切片清单；不会额外写出 manifest。
     */
    const TopologySliceExportResult& Finalize();

    /** @return 已调度的精确整数纳秒时刻。 */
    const std::vector<int64_t>& GetScheduledTimesNs() const;

    /** @return 当前已经写出的结果。 */
    const TopologySliceExportResult& GetResult() const;

  private:
    void WriteScheduledSlice(int64_t expectedTimeNs);

    uint64_t m_linkBandwidthBps;
    std::filesystem::path m_outputDirectory;
    const OnlineOrbitConstellation* m_constellation;
    const CircularOrbitTopologyPolicy* m_policy;
    std::vector<int64_t> m_scheduledTimesNs;
    TopologySliceExportResult m_result;
    bool m_initialized{};
    bool m_finalized{};
};

} // namespace ns3

#endif // SATCOMPUTE_TOPOLOGY_SLICE_EXPORTER_H
