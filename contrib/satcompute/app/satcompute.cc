/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/satcompute-version.h"

#include <iostream>

using namespace ns3;

int
main(int argc, char* argv[])
{
    CommandLine command(__FILE__);
    command.Parse(argc, argv);

    std::cout << "{\"application\":\"satcompute\",\"scenario_schema_version\":\""
              << GetSatComputeSchemaVersion() << "\",\"status\":\"ready\"}" << std::endl;
    return 0;
}
