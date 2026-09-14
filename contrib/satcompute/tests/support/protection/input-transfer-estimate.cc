/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/network-transfer-engine.h"
#include "checkpoint-bound-witness.h"

#include <cstdint>
#include <iostream>
#include <string_view>

/** Offline TSV bridge to the existing pure estimator. No simulator or live path query. */
int
main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--checkpoint-bound-witness")
        return CheckpointBoundWitness();
    if (argc != 1) return 2;
    uint64_t id, bytes, rate;
    int64_t propagation;
    int admissible, local;
    while (std::cin >> id)
    {
        if (!(std::cin >> bytes >> rate >> propagation >> admissible >> local) ||
            (admissible != 0 && admissible != 1) || (local != 0 && local != 1))
            return 2;
        ns3::AdmissiblePathEstimate estimate;
        estimate.admissible = admissible;
        estimate.local = local;
        estimate.path.admittedRateBps = rate;
        estimate.propagationNs = propagation;
        const auto result = estimate.TransferTimeNs(bytes);
        std::cout << id << '\t';
        if (result) std::cout << *result;
        else std::cout << "UNKNOWN";
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 2;
}
