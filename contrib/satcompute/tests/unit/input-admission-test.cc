/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/policy/compfrr/input/input-admission-policy.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
using namespace ns3;
using namespace ns3::protection;
using Json = nlohmann::json;

int main()
{
    try
    {
        Json records; std::cin >> records;
        Json results = Json::array();
        for (const auto& r : records)
        {
            InputAdmissionInput in;
            in.startNs = r.at("start_time_ns"); in.remainingNs = r.at("remaining_compute_ns");
            in.bytes = r.at("input_bytes");
            const auto& p = r.at("predictor");
            in.firstSampleNs = p.at("first_sample_time_ns"); in.finishExclusive = p.at("finish_exclusive");
            in.prediction.emplace(); in.prediction->predictedFailureProbability = p.at("P_F");
            for (const auto& s : p.at("future_steps"))
                in.prediction->steps.push_back({s.at("time_ns"), s.at("q_f1"), s.at("q_f2"), s.at("q_comp")});
            const auto& path = r.at("input_path");
            in.path.admissible = path.at("admissible"); in.path.local = path.at("local_delivery");
            if (!path.at("admitted_rate_bps").is_null()) in.path.path.admittedRateBps = path.at("admitted_rate_bps");
            if (!path.at("propagation_ns").is_null()) in.path.propagationNs = path.at("propagation_ns");
            const auto s = EvaluateInputAdmission(InputAdmissionPolicy::SER_SYMMETRIC_BREAK_EVEN, in);
            // Historical NET comparison is test-only; there is no online NET policy.
            const bool historicalNet = s.reason == "LOCAL_DELIVERY" || s.networkGainNs > s.costNs;
            results.push_back({{"task_id", r.at("task_id")}, {"ser", s.send}, {"net", historicalNet},
                {"serialization_ns", s.serializationNs}, {"network_ns", s.networkReadyNs},
                {"serial_gain_ns", s.serialGainNs}, {"network_gain_ns", s.networkGainNs}, {"cost_ns", s.costNs}});
        }
        std::cout << results.dump() << '\n';
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
