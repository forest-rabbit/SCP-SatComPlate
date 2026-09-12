"""G1 formulas and byte ledgers, without ns-3, datasets, or model downloads."""

from dataclasses import replace
from fractions import Fraction
from pathlib import Path
import sys
import unittest


GENERATION = Path(__file__).resolve().parents[2] / "tools/generation"
sys.path.insert(0, str(GENERATION))
import task_workload_model as model


class WorkloadModelTests(unittest.TestCase):
    def test_size_mapping_and_integer_rounding(self):
        for size, expected in ((1, 1), (666, 1), (667, 2), (999, 2), (1001, 2),
                               (10_000_000, 15_000), (50_000_000, 75_000),
                               (100_000_000, 150_000), (500_000_000, 750_000),
                               (1_000_000_000, 1_500_000)):
            for reference in model.IMAGE_REFERENCES:
                budget = model.image_budget(reference.profile, size, "1")
                self.assertEqual(budget.compute_work_units, expected)
        self.assertEqual(model.image_work_units(1001, Fraction(3, 2)), 3)
        self.assertEqual(model.image_work_units(1 << 20), 1573)
        self.assertEqual(model.image_work_units(1_000_000), 1500)
        for size, nanoseconds in ((10_000_000, 150_000_000), (50_000_000, 750_000_000),
                                  (100_000_000, 1_500_000_000), (500_000_000, 7_500_000_000),
                                  (1_000_000_000, 15_000_000_000)):
            self.assertEqual(model.service_time_ns(model.image_work_units(size), 100_000), nanoseconds)
        self.assertEqual(model.service_time_ns(model.image_work_units(1), 100_000), 10_000)

    def test_image_normalization_preserves_bytes_and_recomputes_sigma(self):
        for reference in model.IMAGE_REFERENCES:
            for size in (1001, reference.input_bytes, 100_000_000, 1_000_000_000):
                current = model.image_budget(reference.profile, size, "same-label")
                old_scale = model.image_budget(reference.profile, size, "same-label", Fraction(2, 3))
                self.assertEqual(old_scale.compute_work_units, model.ceil_div(size, 1000))
                for field in ("payload_bytes", "index_bytes", "output_bytes", "header_bytes",
                              "k_variable_bytes", "rho_variable"):
                    self.assertEqual(getattr(current, field), getattr(old_scale, field))
                self.assertEqual(current.sigma_variable_bytes_per_work_unit,
                                 old_scale.sigma_variable_bytes_per_work_unit *
                                 Fraction(old_scale.compute_work_units, current.compute_work_units))

    def test_mapping_is_monotone_and_independent_of_task_set_and_label(self):
        sizes = [13, 1001, 1001, 500_009, 100_000_000, 1_000_000_000]
        original = [model.image_work_units(size) for size in sizes]
        self.assertEqual(original, sorted(original))
        self.assertEqual(original, list(reversed([model.image_work_units(size)
                                                 for size in reversed(sizes)])))
        small = model.image_budget("dense-image", sizes[0], "a")
        other = model.image_budget("dense-image", sizes[0], "a-longer-label")
        self.assertEqual(small.compute_work_units, other.compute_work_units)
        self.assertEqual(small.k_variable_bytes, other.k_variable_bytes)
        self.assertNotEqual(small.header_bytes, other.header_bytes)

    def test_measured_reference_bytes_and_output_contracts(self):
        labels = ("dense-image-small-001", "sparse-inference-extended-granularity",
                  "compression-small-001")
        for reference, label, expected_h in zip(model.IMAGE_REFERENCES, labels, (65, 85, 65)):
            budget = model.image_budget(reference.profile, reference.input_bytes, label)
            self.assertEqual(budget.payload_bytes, reference.payload_bytes)
            self.assertEqual(budget.index_bytes, reference.index_bytes)
            self.assertEqual(budget.output_bytes, reference.output_bytes)
            self.assertEqual(budget.header_bytes, expected_h)
            self.assertEqual(budget.rho_variable, reference.rho_variable)
            self.assertEqual(budget.sigma_variable_bytes_per_work_unit,
                             Fraction(budget.k_variable_bytes, budget.compute_work_units))
        dense = model.image_budget("dense-image", 52_428_800, "x")
        self.assertLess(dense.output_bytes, dense.k_variable_bytes)
        self.assertEqual(model.image_budget("dense-image", 8, "星").header_bytes, 47)

    def test_image_partition_conservation_and_zero_variable_interval(self):
        for reference in model.IMAGE_REFERENCES:
            budget = model.image_budget(reference.profile, 123_456_789, "9")
            for parts in (1, 3, 5, 10, 20, 101):
                work = [budget.compute_work_units * index // parts for index in range(parts + 1)]
                states = [budget.state_at_work(item) for item in work]
                self.assertEqual(sum(right - left for left, right in zip(states, states[1:])),
                                 budget.k_variable_bytes)
        sparse = model.image_budget("sparse-inference", 1000, "9", Fraction(100))
        self.assertEqual(sparse.state_at_work(1), 0)
        self.assertGreater(sparse.header_bytes, 0)
        self.assertGreater(sparse.k_variable_bytes, 0)

    def test_llm_formula_gqa_dtype_and_no_prompt_double_count(self):
        parameters = model.LlmParameters()
        self.assertEqual(parameters.kv_bytes_per_token, 114_688)
        for tokens in (0, 1, 100, 1500):
            self.assertEqual(parameters.kv_bytes(tokens), tokens * 114_688)
        self.assertEqual(replace(parameters, bytes_per_element=4).kv_bytes(100),
                         parameters.kv_bytes(100) * 2)
        self.assertEqual(replace(parameters, kv_heads=4).kv_bytes(100),
                         parameters.kv_bytes(100) // 2)
        budget = model.llm_budget(768, 200, 500, parameters)
        self.assertEqual(budget.compute_work_units, 280_000)
        self.assertEqual(budget.k_variable_bytes, 700 * 114_688)
        self.assertEqual(budget.output_bytes, 2000)
        self.assertIsNone(budget.rho_variable)
        self.assertEqual(budget.sigma_variable_bytes_per_work_unit, Fraction(114_688, 400))
        self.assertEqual(budget.state_at_work(80_000), parameters.kv_bytes(200))
        self.assertEqual(budget.state_at_work(80_399), parameters.kv_bytes(200))
        self.assertEqual(budget.state_at_work(80_400), parameters.kv_bytes(201))
        self.assertEqual(budget.state_at_work(399), 0)
        self.assertEqual(budget.state_at_work(400), 114688)
        self.assertEqual(budget.state_at_work(800), 229376)
        initial = budget.state_at_work(80_000)
        increments = [budget.state_at_work(end) - budget.state_at_work(end - 400)
                      for end in range(80_400, 280_001, 400)]
        self.assertEqual(initial + sum(increments), budget.k_variable_bytes)
        self.assertEqual(model.llm_budget(9999, 200, 500), replace(budget, input_bytes=9999))
        for tokens in (5000, 7500, 10000):
            revised = model.llm_budget(768, 200, tokens - 200)
            self.assertEqual(revised.k_variable_bytes, tokens * 114_688)
            self.assertEqual(model.service_time_ns(revised.compute_work_units, 100_000), tokens * 4_000_000)
            self.assertEqual(revised.sigma_variable_bytes_per_work_unit, Fraction(114_688, 400))

    def test_service_time_matches_platform_nanosecond_contract(self):
        self.assertEqual(model.service_time_ns(5_000_000, 1_500_000), 3_333_333_334)
        self.assertEqual(model.service_time_ns(70_000, 10_000), 7_000_000_000)
        self.assertEqual(model.service_time_ns(1, model.UINT64_MAX), 1)
        self.assertEqual(model.service_time_ns(model.INT64_MAX, 1_000_000_000),
                         model.INT64_MAX)

    def test_legal_boundaries_and_three_state_budget_partitions(self):
        budgets = [(model.image_budget("dense-image", 52_428_800, "d"),
                    model.uniform_unit_ends(52_428_800, 524_288)),
                   (model.image_budget("sparse-inference", 7000, "s"),
                    model.sample_unit_ends((1000, 2000, 500, 3500))),
                   (model.llm_budget(500, 200, 1300), model.uniform_unit_ends(1500, 1))]
        for budget, ends in budgets:
            for interval in (50, 100, 200):
                with self.subTest(profile=budget.task_profile, interval=interval):
                    records = model.state_budget_points(budget, ends, interval)
                    self.assertEqual(sum(row.delta_work_units for row in records),
                                     budget.compute_work_units)
                    self.assertEqual(sum(row.delta_variable_bytes for row in records),
                                     budget.k_variable_bytes)
                    self.assertEqual(sum(row.delta_total_bytes for row in records),
                                     budget.k_variable_bytes + len(records) * budget.header_bytes)
                    self.assertEqual(len({row.completed_extent for row in records}), len(records))
                    self.assertEqual(records[-1].completed_extent, budget.extent)
                    self.assertTrue(all(row.delta_work_units > 0 for row in records))
                    self.assertTrue(all(row.completed_extent in ends for row in records))
                    self.assertTrue(all(row.completed_extent * 1000 >=
                                        budget.extent * row.nominal_progress_per_mille
                                        for row in records))
        sparse, ends = budgets[1]
        self.assertEqual(model.state_budget_points(sparse, ends, 50)[0].completed_extent, 1000)

    def test_tiny_task_coalesces_zero_work_boundaries_without_losing_completion(self):
        budget = model.image_budget("dense-image", 8, "tiny")
        records = model.state_budget_points(budget, (2, 4, 6, 8), 50)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0].completed_extent, 8)
        self.assertEqual(records[0].delta_variable_bytes, budget.k_variable_bytes)

    def test_invalid_values_and_overflows_are_rejected(self):
        for bad in (True, False, 0, -1, 1.5, "1000", model.UINT64_MAX + 1):
            with self.subTest(value=bad):
                with self.assertRaises(ValueError):
                    model.image_work_units(bad)
        for bad in (1.0, 0.1, Fraction(0), Fraction(-1)):
            with self.assertRaises(ValueError):
                model.image_work_units(1000, bad)
        for call in (
            lambda: model.image_reference("image-enhancement"),
            lambda: model.image_budget("dense-image", 100, ""),
            lambda: model.image_work_units(model.UINT64_MAX, Fraction(2000)),
            lambda: model.image_budget("dense-image", model.UINT64_MAX, "x"),
            lambda: model.LlmParameters(bytes_per_element=0),
            lambda: model.LlmParameters(work_units_per_token=1.5),
            lambda: model.LlmParameters(layers=model.UINT64_MAX),
            lambda: model.LlmParameters(header_bytes=-1),
            lambda: model.llm_budget(100, 0, 10),
            lambda: model.llm_budget(100, 10, 40960),
            lambda: model.llm_budget(100, 10, 10,
                                    model.LlmParameters(work_units_per_token=model.UINT64_MAX)),
            lambda: model.state_budget_points(
                model.llm_budget(100, 1, 1, model.LlmParameters(header_bytes=model.UINT64_MAX)),
                (2,), 100),
            lambda: model.service_time_ns(model.UINT64_MAX, 1),
            lambda: model.service_time_ns(1, 0),
            lambda: model.sample_unit_ends((100, 0)),
            lambda: model.sample_unit_ends(()),
        ):
            with self.subTest(call=call), self.assertRaises(ValueError):
                call()
        budget = model.llm_budget(100, 10, 10)
        for ends in ((), (1, 1, 20), (1, 21), (1, 19), (True, 20)):
            with self.assertRaises(ValueError):
                model.state_budget_points(budget, ends, 100)
        for bad_interval in (True, 0, -1, 1001, 10.5):
            with self.assertRaises(ValueError):
                model.state_budget_points(budget, (20,), bad_interval)


if __name__ == "__main__":
    unittest.main()
