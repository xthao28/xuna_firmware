# Grid sweep of Life Lab needs/FSM knobs against the Proposed life-feel
# targets. Usage: python sim/life_sweep.py <path-to-life_timeline executable>
import itertools, subprocess, sys, csv, io
exe = sys.argv[1]
def run(profile, args):
    out = subprocess.run([exe, "--profile", profile, "--hours", "8"] + args, capture_output=True, text=True, check=True).stdout
    return next(csv.DictReader(io.StringIO(out)))
def f(r, k): return float(r[k])
grid = itertools.product([39321, 41943, 45875], [4096, 5120, 6144], [5, 10, 15, 20], [3072, 4096], [52428], [False, True])
rows = []
for rest_exit, rec, cost, stim, wake, resume in grid:
    args = ["--rest-exit", str(rest_exit), "--recovery", str(rec), "--cost", str(cost), "--stim", str(stim), "--wake", str(wake)] + (["--resume-rest"] if resume else [])
    i, c, a = (run(p, args) for p in ("idle", "casual", "active"))
    lively = lambda r: f(r, "excited_pct") + f(r, "curious_pct")
    down = lambda r: f(r, "resting_pct") + f(r, "sleeping_pct")
    checks = {
        "idle_rest": f(i, "resting_pct") <= 20, "idle_lowE": f(i, "low_energy_pct") <= 30, "idle_calm": f(i, "calm_pct") >= 70,
        "cas_down": 10 <= down(c) <= 30, "cas_lively": 10 <= lively(c) <= 40, "cas_over": f(c, "overwhelmed_pct") <= 5, "cas_lowE": f(c, "low_energy_pct") <= 35,
        "act_down": 20 <= down(a) <= 45, "act_over": f(a, "overwhelmed_pct") <= 15, "act_acc": f(a, "accepted_pct") >= 80, "act_sleep": f(a, "sleeping_pct") <= 30,
    }
    rows.append((sum(checks.values()), [k for k, v in checks.items() if not v], (rest_exit, rec, cost, stim, wake, resume),
                 (f(i,"resting_pct"), f(i,"low_energy_pct"), f(i,"calm_pct")), (down(c), lively(c), f(c,"overwhelmed_pct"), f(c,"low_energy_pct")),
                 (down(a), f(a,"overwhelmed_pct"), f(a,"accepted_pct"), f(a,"sleeping_pct"), lively(a))))
rows.sort(key=lambda r: -r[0])
print("max checks 11; configs", len(rows), "; full pass:", sum(1 for r in rows if r[0] == 11))
for r in rows[:8]:
    print(r[0], "fail=", r[1], "cfg(restExit,recov,cost%,stim,wake,resume)=", r[2])
    print("    idle(rest,lowE,calm)=", r[3], " casual(down,lively,over,lowE)=", tuple(round(x,1) for x in r[4]), " active(down,over,acc,sleep,lively)=", tuple(round(x,1) for x in r[5]))
