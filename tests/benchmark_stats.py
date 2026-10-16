"""Paired host-time statistics shared by system benchmark drivers."""

import math
import statistics

from bench import t_975


def paired_log_interval(reference_seconds, candidate_seconds):
    """Return the ratio and two-sided 95% t interval of paired log ratios."""
    reference = list(reference_seconds)
    candidate = list(candidate_seconds)
    if len(reference) != len(candidate) or len(reference) < 2:
        raise ValueError("at least two complete pairs are required")
    for value in reference + candidate:
        if (
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not math.isfinite(value)
            or value <= 0
        ):
            raise ValueError("wall times must be finite positive numbers")
    logs = [math.log(r) - math.log(c) for r, c in zip(reference, candidate)]
    mean = statistics.mean(logs)
    margin = (
        t_975(len(logs) - 1) * statistics.stdev(logs) / math.sqrt(len(logs))
    )
    try:
        values = [math.exp(x) for x in (mean, mean - margin, mean + margin)]
    except OverflowError as error:
        raise ValueError("ratio interval exceeds numeric range") from error
    if not all(math.isfinite(x) and x > 0 for x in values):
        raise ValueError("ratio interval exceeds numeric range")
    return dict(zip(("ratio", "lower", "upper"), values), n=len(logs))


def amdahl_bound(fraction, speedup, scope):
    """Describe a hypothetical bound using a measured fraction of one scope."""
    if scope not in ("host-wall", "total-process-cpu"):
        raise ValueError("fraction must cover the same wall or total CPU scope")
    if (
        isinstance(fraction, bool)
        or not isinstance(fraction, (int, float))
        or not math.isfinite(fraction)
        or not 0 <= fraction <= 1
        or isinstance(speedup, bool)
        or not isinstance(speedup, (int, float))
        or math.isnan(speedup)
        or speedup < 1
    ):
        raise ValueError("invalid measured fraction or hypothetical speedup")
    denominator = (1 - fraction) + fraction / speedup
    bound = 1 / denominator if denominator else None
    return {
        "fraction": fraction,
        "scope": scope,
        "assumed_speedup": speedup if math.isfinite(speedup) else "infinite",
        "upper_bound": bound if bound is not None else "unbounded",
        "assumptions": (
            "Hypothetical upper bound if the measured fraction improves by at "
            "most assumed_speedup and remaining time and work stay constant. "
            "A hot function alone "
            "does not establish a cause or an achievable speedup."
        ),
    }
