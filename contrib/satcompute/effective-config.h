/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_EFFECTIVE_CONFIG_H
#define SATCOMPUTE_EFFECTIVE_CONFIG_H

#include <filesystem>
#include <stdexcept>

namespace ns3
{

struct ResolvedSatComputeConfig;

/** effective-config.json 无法完整、原子地写出时抛出的异常。 */
class EffectiveConfigError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/**
 * 写出解析后的平台参数及所有实际输入的路径和 SHA-256。
 *
 * 该文件只作为只读运行证据，不是下一次运行的配置入口。
 *
 * @param config 唯一的内部运行配置。
 * @param validateOnly 是否选择只校验而不启动仿真。
 * @param topologyOnly 是否选择只生成拓扑切片。
 * @return 写出的 effective-config.json 绝对路径。
 * @throws EffectiveConfigError 输出或输入清单无法完成时抛出。
 */
std::filesystem::path WriteEffectiveConfig(const ResolvedSatComputeConfig& config,
                                           bool validateOnly = false,
                                           bool topologyOnly = false);

} // namespace ns3

#endif // SATCOMPUTE_EFFECTIVE_CONFIG_H
