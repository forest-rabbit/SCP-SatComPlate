/* SPDX-License-Identifier: GPL-2.0-only */
#include "task-state-adapter.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
/** Exact product/division with optional ceiling and checked uint64 result. */
uint64_t
Scale(uint64_t value, uint64_t multiplier, uint64_t divisor, bool ceiling = false)
{
    const unsigned __int128 product = static_cast<unsigned __int128>(value) * multiplier;
    const auto result = product / divisor + (ceiling && product % divisor != 0);
    if (result > std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("task state byte/WU overflow");
    return static_cast<uint64_t>(result);
}

/** Checked byte sum. */
uint64_t
Add(uint64_t a, uint64_t b)
{
    if (b > std::numeric_limits<uint64_t>::max() - a)
        throw std::overflow_error("task state byte sum overflow");
    return a + b;
}
} // namespace

TaskStateAdapter::TaskStateAdapter(const TaskDefinition& task)
    : m_input(task.inputBytes), m_work(task.computeWorkUnits),
      m_llm(task.taskProfile == TaskProfile::LLM)
{
    if (!task.taskId || !m_input || !m_work)
        throw std::invalid_argument("protection needs positive task ID, INPUT and WU");
    if (m_llm)
    {
        if (m_work % 100 || m_work / 100 > 40960)
            throw std::invalid_argument("LLM must use 100 WU/token and frozen context bound");
        m_extent = m_work / 100;
        m_variable = Scale(m_extent, 114688, 1);
        for (uint64_t token = 1; token <= m_extent; ++token)
        {
            m_boundaries.push_back(token * 100);
            m_ends.push_back(token);
        }
        return;
    }
    if (m_work != Scale(m_input, 3, 2000, true))
        throw std::invalid_argument("image task differs from frozen WU mapping");
    uint64_t reference = 52428800, state;
    switch (task.taskProfile)
    {
    case TaskProfile::DENSE_IMAGE:
        state = 52429200;
        m_header = 44;
        break;
    case TaskProfile::COMPRESSION:
        state = 28441644;
        m_header = 44;
        break;
    case TaskProfile::SPARSE_INFERENCE:
        reference = 26246291;
        state = 49056;
        m_header = 48;
        break;
    default:
        throw std::invalid_argument("protection requires explicit task profile");
    }
    m_header += std::to_string(task.taskId).size();
    m_extent = m_input;
    m_variable = Scale(m_input, state, reference);
    const bool sparse = task.taskProfile == TaskProfile::SPARSE_INFERENCE;
    const uint64_t count = sparse ? std::min(m_input, Scale(m_input, 100, reference, true))
                                  : (m_input - 1) / 524288 + 1;
    for (uint64_t i = 1; i <= count; ++i)
    {
        const auto end = sparse ? i * (m_input / count) + std::min(i, m_input % count)
                                : (i == count ? m_input : i * 524288);
        const auto work = Scale(m_work, end, m_input, true);
        if (work == m_boundaries.back())
            m_ends.back() = end;
        else
        {
            m_boundaries.push_back(work);
            m_ends.push_back(end);
        }
    }
}

void
TaskStateAdapter::CheckWork(uint64_t work) const
{
    if (work > m_work)
        throw std::invalid_argument("protection progress exceeds task work");
}

uint64_t
TaskStateAdapter::StateBytes(uint64_t work) const
{
    CheckWork(work);
    return m_llm ? (work / 100) * 114688 : Scale(m_variable, work, m_work);
}

uint64_t
TaskStateAdapter::CommittedStateBytes(uint64_t work) const
{
    CheckWork(work);
    // ceil(S*(W-w)/W): retain fractional input bytes conservatively.
    return m_llm ? StateBytes(work) : Add(m_input - Scale(m_input, work, m_work), StateBytes(work));
}

uint64_t
TaskStateAdapter::RecordBytes(uint64_t from, uint64_t to) const
{
    if (from >= to || Floor(from) != from || Floor(to) != to)
        throw std::invalid_argument("checkpoint requires increasing legal boundaries");
    return Add(StateBytes(to) - StateBytes(from), m_header);
}

uint64_t
TaskStateAdapter::Floor(uint64_t work) const
{
    CheckWork(work);
    return *std::prev(std::upper_bound(m_boundaries.begin(), m_boundaries.end(), work));
}

std::optional<uint64_t>
TaskStateAdapter::Next(uint64_t current, uint64_t triggered, uint32_t deltaPermille) const
{
    CheckWork(current);
    CheckWork(triggered);
    if (!deltaPermille || deltaPermille > 1000)
        throw std::invalid_argument("checkpoint delta must be in (0,1000] per mille");
    const auto base = std::max(current, triggered);
    const unsigned __int128 fraction = static_cast<unsigned __int128>(base) * 1000 +
                                       static_cast<unsigned __int128>(m_work) * deltaPermille;
    const unsigned __int128 denominator = static_cast<unsigned __int128>(m_work) * 1000;
    if (fraction > denominator)
        return std::nullopt;
    if (fraction > std::numeric_limits<unsigned __int128>::max() / m_extent)
        throw std::overflow_error("checkpoint target arithmetic overflow");
    const auto numerator = static_cast<unsigned __int128>(m_extent) * fraction;
    const auto target =
        static_cast<uint64_t>(numerator / denominator + (numerator % denominator != 0));
    const auto it = std::lower_bound(m_ends.begin(), m_ends.end(), target);
    if (it == m_ends.end())
        return std::nullopt;
    const auto work = m_boundaries.at(it - m_ends.begin());
    return work > triggered ? std::optional<uint64_t>(work) : std::nullopt;
}

ProtectionCosts
GetProtectionCosts(uint64_t fullVariableBytes)
{
    if (fullVariableBytes <= 100000000)
        return {100000, 500000};
    if (fullVariableBytes <= 500000000)
        return {500000, 2000000};
    return {2000000, 8000000};
}
} // namespace ns3::protection
