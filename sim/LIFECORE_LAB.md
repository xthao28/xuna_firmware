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
`powercut`. Since the S18 preparation (2026-10-15) `LifeCore` runs the product
learner (v3, 3 × 5 × 3) with LifeSnapshot v2. `button A` is the GPIO0 greeting
(XUNA chooses; device decision IDs start at 2147483649), `button B` a direct
idle request. The snapshot lab is LifeCore's committer (plan §9.5): feedback is
committed to the in-memory v2 A/B slots before it is applied, and a failed
commit leaves RAM unchanged with the decision still pending. `powercut` tears
the next commit (half the bytes land), and `reboot` runs `SnapshotStore::
recover` (newest valid slot). The CSV reports requests, failures, reboot
count, final feedback count and generation. `sync <unix seconds> <tz
minutes>` (S10 host part) calls `LifeCore::sync`; after it, decisions learn
in day-part cells, and the CSV ends with `sync_accepted,sync_rejected`.

Since S19a (2026-10-19) `day <n>` lives n days in one-minute steps (past the
`millis()` wrap), `reboot` restarts the core clock at 0, and `expect <path>
<op> <value>` checks state (exit code 1 on a failure). After the CSV row the
runner prints the snapshot SHA-256 and the last decision's explanation. Full
grammar and outputs: [README.md](README.md). The runner does not write
NVS/LittleFS or claim board validation.
