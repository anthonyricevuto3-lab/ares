# Flight-side memory

ARES does not measure whole-process resident set size. The flight computer's important bound is that the structures below are fixed tables. They do not grow with mission length. Initialization and the offline replay parser may allocate. A periodic flight cycle does not append to a `std::vector`.

`tests/unit/resource_budget_test.cpp` checks the capacities and rejects an object that grows past a small ceiling. Those ceilings are regression guards, not a datasheet of padding on every ABI.

| Structure | Capacity | What happens when full |
| --- | --- | --- |
| FaultRegistry | 19 logical faults | A new identity is rejected and `saturated()` latches. An active fault is not evicted |
| EventLog | 64 system events | The oldest event is overwritten |
| Cycle log | 64 task cycles | The oldest cycle is overwritten. This is not a process failure |
| Deadline-miss log | 32 misses | The oldest miss is overwritten. That is exit code 9 when no worker fault outranks it |
| Chaos schedule | 16 events | A larger schedule is rejected at load |
| Chaos edge notes | 32 edges | Copy into a short caller buffer sets `truncated`. Injection state does not depend on that copy |
| FlightRecorder | 256 records | The last slot is reserved for MissionEnd. Earlier records stay. Sequence numbers do not wrap |
| Recovery episodes | 2 (navigation and GPS) | Attempt caps are 2 navigation restarts and 1 GPS switch |
| FaultMailbox | one slot per source | A new observation replaces the pending one. It is not a queue |

The production recorder object is a fixed prefix on the order of tens of kilobytes, plus one mutex. `encode()` allocates the file image once, on the main thread, after workers have joined.

## Allocation on the periodic path

These paths do not allocate to do their job:

- `FaultRegistry` raise and clear
- `RecoveryManager` observe
- `GpsSelector` failover and read
- `FlightRecorder` append
- Chaos schedule lookup after load
- `PeriodicTask::poll` schedule arithmetic

`Logger` formats a line into a `std::string` when a message is emitted. That is existing logging, not flight-state growth. The end-of-mission summary also formats strings, once, after `shutdown()` joins the workers. Replay parses offline and may allocate the record vector.
