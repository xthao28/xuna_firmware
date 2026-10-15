# LifeCore Lab runner

`lifecore_lab.cpp` is a host-only preparation runner for the bounded
`LifeCoreSession` path. It uses the existing `@+<ms>` parser and emits one CSV
row for deterministic replay.

Build when a C++11 compiler is available:

```text
c++ -std=c++11 -Wall -Wextra -Werror -Ilib/xuna_core/include \
  sim/lifecore_lab.cpp -o lifecore_lab
```

Supported trace verbs are `choose`, `feedback`, `button`, `cmd`, `reboot` and
`powercut`. The snapshot lab is LifeCore's committer (plan §9.5): feedback is
committed to the in-memory A/B slots before it is applied, and a failed commit
leaves RAM unchanged with the decision still pending. `powercut` arms the next
commit to fail, and
`reboot` recovers the newest valid committed slot. The CSV reports requests,
failures, reboot count, final event count and generation.

The reserved `sync`, `day` and `expect` verbs are counted as unsupported until
their trace/storage contracts are frozen. The runner does not write
NVS/LittleFS or claim board validation.