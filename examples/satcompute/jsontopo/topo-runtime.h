#ifndef TOPO_RUNTIME_H
#define TOPO_RUNTIME_H

#include <string>

namespace ns3 {

struct LinkOutputSnapshot;

void ConfigureDefaultJsonTopologyFiles();
void ScheduleTopologyTimeSlices();
bool IsLinkOutputMode();
std::string GetLinkOutputInitialSnapshotFile();
LinkOutputSnapshot ReadConfiguredLinkOutputInitialSnapshot();

} // namespace ns3

#endif
