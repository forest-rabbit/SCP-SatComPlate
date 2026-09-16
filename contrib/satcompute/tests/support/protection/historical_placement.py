"""Historical evidence compatibility only; never used by production execution.

Frozen output names and old placement identities stay immutable. Current readers
prefer canonical files; simultaneous old/new files are rejected as ambiguous.
"""
from pathlib import Path

FILES = {
    'n5c-placement-decisions.csv': 'compfrr-placement-decisions.csv',
    'n5c-recent-u-history.csv': 'compfrr-recent-u-history.csv',
    'n5c-rational-u-history.csv': 'compfrr-rational-u-history.csv',
}
REASONS = {f'N5C_{suffix}': f'COMPFRR_P_{suffix}' for suffix in (
    'NO_FEASIBLE_REMOTE', 'POST_BATCH_NODE_UNAVAILABLE', 'POST_BATCH_LOCAL_STORAGE',
    'POST_BATCH_QUOTA', 'POST_BATCH_DEADLINE')}


def evidence_path(root, name):
    canonical = FILES.get(name, name)
    historical = next((old for old, new in FILES.items() if new == canonical), canonical)
    current, old = Path(root) / canonical, Path(root) / historical
    if current != old and current.exists() and old.exists():
        raise ValueError('ambiguous old/new placement evidence')
    return current if current.exists() else old


def canonical_gate_path(path):
    """Explicit small-fixture/output path renames, not a general text replacement."""
    parts = []
    for part in Path(path).parts:
        part = FILES.get(part, part)
        if part.startswith('online-n5c'):
            part = part.replace('online-n5c', 'online-compfrr-placement', 1)
        elif part.startswith('n5c-boundary-'):
            part = part.replace('n5c-boundary-', 'compfrr-placement-boundary-', 1)
        parts.append(part)
    return Path(*parts)


def canonical_gate_scalar(value):
    if value == 'n5c':
        return 'compfrr'
    return REASONS.get(value, value)


def attach_spatial_audit(report, spatial):
    """Retain historical report keys only for an explicitly historical identity."""
    placement = report['execution']['placement_mode']
    report['n5c' if placement == 'n5c' else 'compfrr_placement'] = (
        spatial() if placement in ('n5c', 'compfrr') else None)
