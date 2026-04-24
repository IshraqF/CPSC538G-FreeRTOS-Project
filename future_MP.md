# Improvements for MP

# Global EDF

## Tighter Admission Control

The current bound for global EDF is LL: `U <= m`. This is necessary and sufficient for implicit deadlines on single core, but only sufficient for global multicore EDF. Tighter test exists (e.g. RTA) but is more computationally expensive.

## Per-Core Deadline Miss Tracking

Currently `prvEDFCheckDeadlineMiss` only runs on core 0's tick ISR, where it checks all cores. Since tick count and TCBs are global, this works fine. As a small optimization, distributing the check across cores could potentially reduce core 0's ISR workload.

---

# Partitioned EDF

## Heuristic

Current implementation uses first-fit heuristic for partitioning. More sophisticated heuristics (e.g. best-fit, worst-fit) or even meta-heuristics (e.g. genetic algorithm) could be explored to achieve better load balancing across cores and higher acceptance ratio.
