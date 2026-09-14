/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CB_SAT_CONTROLLER_H
#define SATCOMPUTE_CB_SAT_CONTROLLER_H
#include "cb-sat-config.h"
#include "cb-sat-recovery.h"

namespace ns3::protection::checkbullet
{
/** CB-only lifecycle; shared services and placement are injected, not reimplemented. */
class CbSatController
{
  public:
    CbSatController(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
                    uint64_t capacity, int64_t stopNs,
                    std::unique_ptr<PlacementPolicy> placement, RemoteBusyRecoveryPolicy busy);
    void Finalize(); ///< Recovery, task service, then remaining normal objects.
    void WriteMetrics(const std::filesystem::path& directory) const;
    static void RemoveOutputs(const std::filesystem::path& directory);
  private:
    Ptr<TaskCoordinator> m_tasks;
    CbMtbfProfile m_profile;
    std::unique_ptr<PlacementPolicy> m_placement;
    PlacementLoadLedger m_loads;
    CbSatManager m_manager;
    CbSatRecovery m_recovery;
    RemoteBusyRecoveryPolicy m_busy;
};
/** Read-only CB evidence writer, also exercised with explicit unit-test managers. */
void WriteCbMetrics(const CbSatManager& manager, const CbSatRecovery& recovery,
                    const std::filesystem::path& directory);
} // namespace ns3::protection::checkbullet
#endif
