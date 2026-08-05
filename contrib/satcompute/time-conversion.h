/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_TIME_CONVERSION_H
#define SATCOMPUTE_TIME_CONVERSION_H

#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace ns3
{

/** 人工时间参数不能无损表示为整数纳秒时抛出的异常。 */
class SatComputeTimeError : public std::invalid_argument
{
  public:
    using std::invalid_argument::invalid_argument;
};

/** 将有限、非负秒数四舍五入为整数纳秒。 */
int64_t SatComputeSecondsToNanoseconds(double seconds, std::string_view fieldName);

/** 将十进制秒字符串精确转换为整数纳秒。 */
int64_t SatComputeDecimalSecondsToNanoseconds(std::string_view token,
                                             std::string_view fieldName,
                                             bool positive = false);

} // namespace ns3

#endif // SATCOMPUTE_TIME_CONVERSION_H
