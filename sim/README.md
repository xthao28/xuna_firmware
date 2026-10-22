# Life Lab (S19a)

Host-only tools that run the real `xuna_core` code on a PC. The S19a Life Lab
is **`lifecore_lab.cpp`** ([LIFECORE_LAB.md](LIFECORE_LAB.md)): it replays a
trace through `LifeCoreSession` + `LifeCore` with an in-memory A/B snapshot
store that simulates power cuts. `lifelab.cpp` is the older learner-only
runner (prequential/shadow metrics) and keeps its name for existing records.

## Build and run

C++11 compiler, no PlatformIO needed:

```text
c++ -std=c++11 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/lifecore_lab.cpp -o lifecore_lab
./lifecore_lab sim/traces/c9_lifelab_days.trace     # or: ./lifecore_lab < trace
```

Exit code: 0 ok, 1 an `expect` failed (details on stderr), 2 invalid trace or
I/O error. Output is deterministic for a given trace (seed 1, stream 3).

## Trace grammar (design freeze, 2026-10-19)

One command per line: `@+<ms> <verb> <args> [# comment]`. Blank lines and
lines starting with `#` are ignored. `<ms>` is a decimal u32 and must not
decrease from one command to the next. Each `day <n>` shifts every later
command by n days, so long runs keep small `@+` values. At most 64 commands.

| Verb | Arguments | Effect |
| --- | --- | --- |
| `choose` | `<id> <context>` | App suggestion (opcode 2): id ≥ 1, context 0 quiet / 1 play |
| `feedback` | `<id> <outcome> <source>` | Rating of decision `<id>`; outcome `positive`/`negative` (also `1/0`, `true/false`, `approve(d)/reject(ed)`); source 0–2 |
| `button` | `A` \| `B` | GPIO0 greeting (XUNA chooses) / GPIO14 direct idle |
| `cmd` | `<hex>` | Direct action request, ActionId 0–3 (`0x` prefix optional) |
| `sync` | `<unix s> <tz min>` | Phone time sync (S10) |
| `reboot` | — | Recover from the A/B store; the core clock (`millis()`) restarts at 0 |
| `powercut` | — | The next store write lands only half its bytes and fails |
| `day` | `<n>` | Live n days (1–3650), one `LifeCore` step per simulated minute |
| `expect` | `<path> <op> <u32>` | Check state; `op` is `==`, `!=`, `>=`, `<=`. A failure is counted and the run continues |

`expect` paths: `feedback_count`, `generation`, `active_seconds`, `activity`,
`visual`, `has_decision`, `clock_quality`, `day_part`, `preferred_quiet`,
`preferred_play`, `preferred_greeting` (learner action index), and the run
counters `choices`, `feedback_applied`, `persist_requests`, `persist_failures`,
`reboots`, `sync_accepted`, `sync_rejected`, `days`. An unknown path makes
the trace invalid (exit 2).

Not simulated: the v1 adapter's active-time accounting and five-minute lazy
checkpoint (`V1LifeCoreAdapter`), BLE, display and NVS/LittleFS. A `day`
therefore writes nothing unless feedback arrives.

## Outputs

1. One CSV header + row (counters, final activity/visual/clock, `days`,
   `expect_checked`, `expect_failed`, `final_mono_ms`). The first 17 columns
   are unchanged from the pre-S19a runner.
2. `snapshot_sha256=<hex>`: SHA-256 of the encoded LifeSnapshot v2.
3. `explanation …` and one `explanation_cell …` per action: the evidence of
   the last shown decision, as BLE READ `000b` projects it (plan §9.6).

## Device card preview (`card_preview.cpp`)

Renders the status and pairing cards with the real fonts and `xuna_core`
code into `status_card.ppm` / `pairing_card.ppm` (320 × 170); build line in
the file header. Re-run after changing `DeviceCards.h` and
`tools/generate_ui_font.py`.

## S21 target parity (`[env:xuna_parity]`)

`src/parity/parity_main.cpp` replays `c3_cut_then_commit`, `c8_dayparts` and
`c9_lifelab_days` (embedded at build time by `tools/embed_parity_traces.py`)
on the ESP32 with the in-RAM store, prints the same lines as `lifecore_lab`
plus `step()` timing, and repeats every 20 s. No BLE, display or NVS. Upload
it, compare with the host, then upload `xuna_t_display_s3` again:

```text
~/.platformio/penv/bin/pio run -d firmware -e xuna_parity -t upload --upload-port /dev/cu.usbmodem14101
```

## S19b scenarios (`scenarios.cpp`)

`xuna/lab/BoardLab.h` replays the board: `BootSequence` (the same code as
`main.cpp` setup()), `V1LifeCoreAdapter` with the production flags,
`IdentityService`, the offline quiet routine and the five-minute checkpoint,
over an in-memory store (A/B slots, identity, `bootc`). A reboot rebuilds
every RAM object. Commands enter as v1 frame bytes. `xuna/lab/LifeScenarios.h`
holds the scenarios; `test/test_life_lab_scenarios` runs them as tests.

```text
c++ -std=c++11 -O2 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/scenarios.cpp -o scenarios
./scenarios     # exit 0 when D, E and stability pass; prints runtimes
```

| Scenario | What it checks |
| --- | --- |
| D | Migrated pet, 100 days, 800 suggestions, greetings, daily syncs, no rating: learner bit-identical to its boot state, in RAM and after a reboot |
| E | Migrated v1 pet, 10 reboots + 5 torn-write power cuts while rating: durable feedback = acks, resent rating never applied twice, PetId unchanged, age loss ≤ one checkpoint (300 s) |
| Stability | 100 days of a seeded owner (ratings, late ratings, greetings, idle, syncs, a reboot every 10 days, one forget): invariants after every 1 s step |
| Routine / random | Owner arrives at 19:00 on weekdays and 09:00 at weekends (or at random times) for 28 days: XUNA must anticipate the routine owner shortly before arrival with no false alarms, and never the random one |
| Reactions | XUNA acts on its own when idle; a simulated owner pets what they like and shoos what they do not, reacting to only part of the choices: the learned quiet choice must follow the owner |

## S20 golden vectors (`golden.cpp`)

`golden.cpp` boots `BoardLab` from a blank v1 snapshot (a migrated pet with
no history), sends 27 v1 command frames (suggestions, ratings, a consumed and
an unknown rating, a direct action, a stated preference, forget) and writes,
per command, the frame, the adapter's result and the life / memory values the
app reads. Output is deterministic.

```text
c++ -std=c++11 -O2 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/golden.cpp -o golden
./golden > sim/golden/learning_v1.json
cp sim/golden/learning_v1.json ../mobile/test/fixtures/firmware/learning_v1.json
```

The mobile copy is checked by `test/features/device/data/firmware_golden_test.dart`
(procedure: `mobile/test/fixtures/firmware/README.md`). Regenerate whenever the
adapter, learner or v1 encoding changes, and commit both copies together.

## Learner-only runner (`lifelab.cpp`)

```text
c++ -std=c++11 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/lifelab.cpp -o lifelab
```

It executes `choose`/`feedback` only and counts every other verb as
`unsupported`. It emits one CSV row with prequential samples/Brier values,
shadow agreement, exploration, feedback rejection and a fingerprint (at most
64 events, eight-entry decision window, no persistence or `LifeCore`).

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

## Migration check (S13 + S18 preparation)

`migration_check.cpp` compares the board's v1 + `BoardLearner` path with the
migrated LifeCore path on real v1 snapshots. Extract them read-only from an NVS
partition dump into a folder outside the backup directory:

```text
python3 tools/nvs_life_dump.py <nvs.bin> --out-dir <work>
c++ -std=c++11 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/migration_check.cpp -o migration_check
./migration_check <work>/slot0.bin <work>/slot1.bin
```
