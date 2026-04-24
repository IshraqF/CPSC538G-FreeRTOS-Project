# Future Improvements for EDF

## Deadline Miss Policy

Currently, deadline miss is logged but tasks keeps running. Other options to consider:
- Skip the late job, reset deadline to next period and yield immediately
- Kill late job

## Enforce WCET

The declared `xTaskComputationTime` is never enforced internally, it's only factored in for admission control calculation. A timer watchdog could be added to preempt tasks that exceed declared WCET for system safety.

## Aperiodic Task Support

EDF currently only handles periodic tasks. Aperiodic or sporadic tasks could be supported via a bandwidth server (e.g., Constant Bandwidth Server) that wraps aperiodic arrivals in a virtual periodic container so they don't starve periodic tasks.

## Runtime Optimization

Current implementation picks the head of a sorted linked list, so O(1) selection and O(n) for insertion. A priority queue/binary heap would give O(log n) insertion and O(1) min-extraction,  which is better for large task set systems.

## Non-EDF Task Starvation Mitigation

EDF tasks unconditionally preempt all fixed-priority tasks. In actually deployed systems (instead of academic purposes), a better approach could be to cap EDF utilization at a threshold (e.g. 95%) and guarantee a minimum bandwidth for nonEDF tasks.
