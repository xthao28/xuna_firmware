# Learning Lab pre-S19a runner

`lifelab.cpp` is a host-only, learning-focused runner built on the same C++
trace parser, shadow harness and prequential evaluator used by firmware tests.
It is an offline preparation slice, not the complete S19a LifeCore simulator.

## Build

When a C++11 compiler is available:

```text
c++ -std=c++11 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/lifelab.cpp -o lifelab
```

## Trace input

Each line uses `@+<ms> <verb> <args>`. Comments start with `#`.

The learner runner executes:

```text
@+10 choose 1 0
@+20 feedback 1 positive 1
@+30 choose 2 0
@+40 feedback 2 rejected 1
```

The parser also reserves the broader S19a verbs `button`, `cmd`, `sync`,
`reboot`, `powercut`, `day` and `expect`; they are parsed but not sent to the
learner yet and are counted as `unsupported` by this pre-S19a binary.

## Output and limits

The runner emits one CSV header and one metrics row containing prequential
samples/Brier values, shadow agreement, exploration, feedback rejection and a
deterministic fingerprint. It keeps at most 64 trace events and an eight-entry
decision window. It does not persist state, emulate NVS/LittleFS, run `LifeCore`
or write to a board.

## Example traces

[traces/](traces/) holds annotated traces for `lifelab` (`l*.trace`) and
`lifecore_lab` (`c*.trace`); the first line of each states the expected result.
Results are recorded in
[lifecore-lab-runs-2026-10-15.md](../../docs/roadmap/lifecore-lab-runs-2026-10-15.md).

## Closed-loop learning bench

`learning_bench.cpp` runs the simulated owner (`xuna/lab/OwnerModel.h`) against
random, static, v1 and v2 learners (`xuna/lab/LearnerBench.h`) for the §9.9
scenarios plus drift, noisy, sparse and 4-action cases:

```text
g++ -std=c++11 -Wall -Wextra -Werror -O2 -Ilib/xuna_core/include sim/learning_bench.cpp -o learning_bench
./learning_bench [--seeds N] [--curves]
```

Results: [life-lab-phase0-2026-10-15.md](../../docs/roadmap/life-lab-phase0-2026-10-15.md).
