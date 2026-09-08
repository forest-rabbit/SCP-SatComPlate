"""N4C work/state budget functions; no simulator, model runtime, or file I/O.

Image reference bytes are measurements from SCP-TaskModeling. Scaling those
bytes to other inputs, WU rates, and the LLM token model are scenario assumptions.
All budget arithmetic uses integers or Fraction, never rounded display ratios.
"""

from bisect import bisect_left
from dataclasses import dataclass
from fractions import Fraction
from typing import Sequence


UINT64_MAX = (1 << 64) - 1
INT64_MAX = (1 << 63) - 1
NS_PER_SECOND = 1_000_000_000
IMAGE_WORK_UNITS_PER_BYTE = Fraction(3, 2000)
TASK_PROFILES = ("dense-image", "sparse-inference", "compression", "llm")
TASK_MODELING_COMMIT = "0dbc0c7b6281219e1356151fd640336cde885e7d"
QWEN_CONFIG_REVISION = "c1899de289a04d12100db370d81485cdf75e47ca"
QWEN_CONFIG_URL = (
    "https://huggingface.co/Qwen/Qwen3-0.6B/blob/"
    + QWEN_CONFIG_REVISION
    + "/config.json"
)


def require_uint(value: int, name: str, minimum: int = 0,
                 maximum: int = UINT64_MAX) -> int:
    """Reject booleans, non-integers, negative values, and overflow."""
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{name} must be an integer in [{minimum}, {maximum}]")
    return value


def ceil_div(numerator: int, denominator: int) -> int:
    """Divide non-negative integers, rounding upward without float conversion."""
    if numerator < 0 or denominator <= 0:
        raise ValueError("ceil_div requires a non-negative numerator and positive denominator")
    return (numerator + denominator - 1) // denominator


@dataclass(frozen=True)
class ImageReference:
    """Measured application bytes; output may include canonical descriptors."""

    profile: str
    input_bytes: int
    payload_bytes: int
    index_bytes: int
    output_bytes: int
    fixed_header_bytes: int
    input_representation: str
    source_document: str

    @property
    def rho_variable(self) -> Fraction:
        """Measured variable-state ratio, excluding repeated headers."""
        return Fraction(self.payload_bytes + self.index_bytes, self.input_bytes)


IMAGE_REFERENCES = (
    ImageReference("dense-image", 52_428_800, 52_428_800, 400, 52_428_800, 44,
                   "raw-four-band-uint16-array", "dense-image-pilot-results.md"),
    ImageReference("sparse-inference", 26_246_291, 48_256, 800, 49_056, 48,
                   "encoded-image-file-bytes", "sparse-inference-pilot-results.md"),
    ImageReference("compression", 52_428_800, 28_440_844, 800, 28_441_644, 44,
                   "raw-four-band-uint16-array", "compression-encoding-pilot-results.md"),
)


def image_reference(profile: str) -> ImageReference:
    """Look up a measured image profile; never infer an old task label."""
    for reference in IMAGE_REFERENCES:
        if reference.profile == profile:
            return reference
    raise ValueError(f"unknown image task profile: {profile!r}")


def image_work_units(input_bytes: int, intensity: Fraction = Fraction(1)) -> int:
    """Apply the G1 candidate size mapping, independent of ID/rank/other tasks."""
    require_uint(input_bytes, "input_bytes", 1)
    if not isinstance(intensity, Fraction) or intensity <= 0:
        raise ValueError("intensity must be a positive Fraction")
    exact = input_bytes * IMAGE_WORK_UNITS_PER_BYTE * intensity
    return require_uint(ceil_div(exact.numerator, exact.denominator), "work_units", 1)


def service_time_ns(work_units: int, rate_work_units_per_second: int) -> int:
    """Mirror ComputeService::CalculateServiceTimeNs integer rounding."""
    require_uint(work_units, "work_units", 1)
    require_uint(rate_work_units_per_second, "rate_work_units_per_second", 1)
    return require_uint(ceil_div(work_units * NS_PER_SECOND, rate_work_units_per_second),
                        "service_time_ns", 1, INT64_MAX)


@dataclass(frozen=True)
class LlmParameters:
    """Public structural inputs plus explicit, unmeasured scenario assumptions."""

    layers: int = 28
    kv_heads: int = 8
    head_dim: int = 128
    bytes_per_element: int = 2
    work_units_per_token: int = 100
    header_bytes: int = 0
    max_cached_tokens: int = 40960

    def __post_init__(self) -> None:
        for name in ("layers", "kv_heads", "head_dim", "bytes_per_element",
                     "work_units_per_token", "max_cached_tokens"):
            require_uint(getattr(self, name), name, 1)
        require_uint(self.header_bytes, "header_bytes")
        require_uint(2 * self.layers * self.kv_heads * self.head_dim * self.bytes_per_element,
                     "kv_bytes_per_token", 1)

    @property
    def kv_bytes_per_token(self) -> int:
        """Raw Key plus Value tensor bytes, not a measured checkpoint blob."""
        return 2 * self.layers * self.kv_heads * self.head_dim * self.bytes_per_element

    def kv_bytes(self, cached_tokens: int) -> int:
        """Count only completed synthetic cache tokens, including prompt tokens."""
        require_uint(cached_tokens, "cached_tokens", 0, self.max_cached_tokens)
        return require_uint(cached_tokens * self.kv_bytes_per_token, "kv_bytes")


@dataclass(frozen=True)
class TaskBudget:
    """Immutable planning quantities, deliberately not a runnable TaskTrace.

    extent is input bytes for images and cached tokens for LLM. Image state is
    a linear mean budget. LLM state advances at complete token boundaries only.
    """

    task_profile: str
    input_bytes: int
    output_bytes: int
    compute_work_units: int
    payload_bytes: int
    index_bytes: int
    header_bytes: int
    extent: int

    def __post_init__(self) -> None:
        if self.task_profile not in TASK_PROFILES:
            raise ValueError("unknown task profile")
        for name in ("input_bytes", "compute_work_units", "extent"):
            require_uint(getattr(self, name), name, 1)
        for name in ("output_bytes", "payload_bytes", "index_bytes", "header_bytes"):
            require_uint(getattr(self, name), name)
        require_uint(self.payload_bytes + self.index_bytes, "k_variable_bytes")
        if self.task_profile == "llm" and (
            self.compute_work_units % self.extent or self.k_variable_bytes % self.extent
        ):
            raise ValueError("LLM uses integer WU and KV bytes per token")

    @property
    def k_variable_bytes(self) -> int:
        """Payload plus recovery indices, excluding repeated H."""
        return self.payload_bytes + self.index_bytes

    @property
    def sigma_variable_bytes_per_work_unit(self) -> Fraction:
        """New variable-only coefficient; not TaskModeling's total-byte sigma."""
        return Fraction(self.k_variable_bytes, self.compute_work_units)

    @property
    def rho_variable(self) -> Fraction | None:
        """Realized integer budget ratio; intentionally undefined for LLM."""
        if self.task_profile == "llm":
            return None
        return Fraction(self.k_variable_bytes, self.input_bytes)

    def work_at_extent(self, completed_extent: int) -> int:
        """Map an application boundary to cumulative integer WU."""
        require_uint(completed_extent, "completed_extent", 0, self.extent)
        return ceil_div(self.compute_work_units * completed_extent, self.extent)

    def state_at_work(self, completed_work_units: int) -> int:
        """Return cumulative bytes without including a partially processed token."""
        require_uint(completed_work_units, "completed_work_units", 0, self.compute_work_units)
        if self.task_profile == "llm":
            tokens = completed_work_units // (self.compute_work_units // self.extent)
            return tokens * (self.k_variable_bytes // self.extent)
        return self.k_variable_bytes * completed_work_units // self.compute_work_units


def image_budget(profile: str, input_bytes: int, task_label: str,
                 intensity: Fraction = Fraction(1)) -> TaskBudget:
    """Scale measured byte ratios once, retaining header and output semantics."""
    reference = image_reference(profile)
    work = image_work_units(input_bytes, intensity)
    if not isinstance(task_label, str) or not task_label:
        raise ValueError("task_label must be a non-empty string")
    payload = input_bytes * reference.payload_bytes // reference.input_bytes
    variable = input_bytes * (reference.payload_bytes + reference.index_bytes) // reference.input_bytes
    output = input_bytes * reference.output_bytes // reference.input_bytes
    return TaskBudget(profile, input_bytes, output, work, payload, variable - payload,
                      reference.fixed_header_bytes + len(task_label.encode("utf-8")), input_bytes)


def llm_budget(input_bytes: int, prompt_tokens: int, generation_tokens: int,
               parameters: LlmParameters = LlmParameters()) -> TaskBudget:
    """Model synthetic tokens; RESULT is an explicitly assumed uint32 token list."""
    require_uint(input_bytes, "input_bytes", 1)
    require_uint(prompt_tokens, "prompt_tokens", 1)
    require_uint(generation_tokens, "generation_tokens", 1)
    tokens = prompt_tokens + generation_tokens
    variable = parameters.kv_bytes(tokens)
    return TaskBudget("llm", input_bytes, 4 * generation_tokens,
                      tokens * parameters.work_units_per_token, variable, 0,
                      parameters.header_bytes, tokens)


def uniform_unit_ends(extent: int, unit_extent: int) -> tuple[int, ...]:
    """Full tiles/tokens plus a final partial unit, expressed as cumulative extent."""
    require_uint(extent, "extent", 1)
    require_uint(unit_extent, "unit_extent", 1)
    return tuple(range(unit_extent, extent, unit_extent)) + (extent,)


def sample_unit_ends(sample_bytes: Sequence[int]) -> tuple[int, ...]:
    """Use whole encoded-image boundaries, allowing unequal file sizes."""
    if not sample_bytes:
        raise ValueError("sample_bytes must be non-empty")
    total = 0
    ends = []
    for size in sample_bytes:
        total = require_uint(total + require_uint(size, "sample_bytes", 1), "total_sample_bytes", 1)
        ends.append(total)
    return tuple(ends)


@dataclass(frozen=True)
class StateBudgetPoint:
    """One diagnostic application-progress point, not an N5 backup object."""

    nominal_progress_per_mille: int
    completed_extent: int
    completed_work_units: int
    delta_work_units: int
    cumulative_variable_bytes: int
    delta_variable_bytes: int
    header_bytes: int

    @property
    def delta_total_bytes(self) -> int:
        """Hypothetical variable-plus-H accounting, not serialized L1 bytes."""
        return require_uint(self.delta_variable_bytes + self.header_bytes, "delta_total_bytes")


def state_budget_points(budget: TaskBudget, unit_ends: Sequence[int],
                        interval_per_mille: int) -> tuple[StateBudgetPoint, ...]:
    """Map abstract progress to successor legal boundaries, without duplicates.

    G1 only samples 50/100/200 per-mille (5/10/20%) to check state conservation.
    These diagnostic points are neither a frequency search nor L1 records.
    """
    require_uint(interval_per_mille, "interval_per_mille", 1, 1000)
    previous_end = 0
    for end in unit_ends:
        require_uint(end, "unit_end", previous_end + 1, budget.extent)
        previous_end = end
    if previous_end != budget.extent:
        raise ValueError("unit_ends must end exactly at task extent")
    # Very small tasks can map multiple physical boundaries to the same WU.
    # Keep the last boundary in each group; even a one-WU task ends at 100%.
    ends_by_work = {budget.work_at_extent(end): end for end in unit_ends}
    legal_ends = tuple(ends_by_work.values())
    records = []
    previous_work = previous_state = 0
    for nominal in (*range(interval_per_mille, 1000, interval_per_mille), 1000):
        target = ceil_div(budget.extent * nominal, 1000)
        end = legal_ends[bisect_left(legal_ends, target)]
        work = budget.work_at_extent(end)
        if work == previous_work:
            continue
        state = budget.state_at_work(work)
        require_uint(state - previous_state + budget.header_bytes, "delta_total_bytes")
        record = StateBudgetPoint(nominal, end, work, work - previous_work,
                                  state, state - previous_state, budget.header_bytes)
        records.append(record)
        previous_work, previous_state = work, state
    return tuple(records)
