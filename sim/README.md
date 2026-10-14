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
