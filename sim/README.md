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
random, static, v1, v2 and v3 (greedy, A ε, C UCB, D progress) learners (`xuna/lab/LearnerBench.h`) for the §9.9
scenarios plus drift, noisy, sparse, 4-action and day-part cases:

```text
g++ -std=c++11 -Wall -Wextra -Werror -O2 -Ilib/xuna_core/include sim/learning_bench.cpp -o learning_bench
./learning_bench [--seeds N] [--gamma G] [--curves]
```

Results: [life-lab-phase0-2026-10-15.md](../../docs/roadmap/life-lab-phase0-2026-10-15.md).
Learner v3 results: [learner-v3-phase1-2026-10-15.md](../../docs/roadmap/learner-v3-phase1-2026-10-15.md).

## Learner v3 parameter sweep

`learner_sweep.cpp` runs a grid of v3 configurations (UCB γ/k/m/bonus, ε-greedy
and learning-progress variants) over 8 scenarios and prints mean regret per
scenario, noisy flips and late-context generalization:

```text
g++ -std=c++11 -Wall -Wextra -Werror -O2 -Ilib/xuna_core/include sim/learner_sweep.cpp -o learner_sweep
./learner_sweep [--seeds N]
```

## Needs and mood timeline

`life_timeline.cpp` runs the real `LifeCore` for hours with an owner profile
and prints a summary row or a per-minute series; `life_sweep.py` grids the
Life Lab needs/FSM knobs against the Proposed life-feel targets:

```text
g++ -std=c++11 -Wall -Wextra -Werror -O2 -Ilib/xuna_core/include sim/life_timeline.cpp -o life_timeline
./life_timeline --profile idle|casual|active --hours 8 [--series] [knobs...]
python sim/life_sweep.py ./life_timeline
```

Results: [life-lab-needs-timeline-2026-10-15.md](../../docs/roadmap/life-lab-needs-timeline-2026-10-15.md).
