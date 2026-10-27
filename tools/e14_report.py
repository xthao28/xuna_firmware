"""Judge an E14 run against the owner-confirmed thresholds (DC-1, 2026-10-27).

    python tools/e14_report.py --baseline logs/e14-baseline.log --run logs/e14-run.log --down-for 10

Reads serial logs captured with `pio device monitor` (any prefix before the
fields is fine). Uses the last cumulative `e14` / `e14_net` lines of each log.
Prints a Markdown table for docs/roadmap/cloud/E14-results-<date>.md and exits 1
if any threshold fails. App-side BLE errors and the router power-cycle are
checked by the owner and entered by hand.
"""
import argparse
import re
import sys
from pathlib import Path

FIELD = re.compile(r"(\w+)=(-?0x[0-9a-fA-F]+|-?\d+|\w+)")


def parse(path):
    last, net, boots, reasons = {}, {}, 0, []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if " e14 t_s=" in f" {line}":
            last = dict(FIELD.findall(line[line.index("e14 t_s="):]))
        elif "e14_net t_s=" in line:
            net = dict(FIELD.findall(line[line.index("e14_net t_s="):]))
        elif "e14_boot " in line:
            boots += 1
            reasons.append(dict(FIELD.findall(line)).get("reset_reason", "?"))
    if not last:
        sys.exit(f"{path}: no 'e14 t_s=' line (built with -e xuna_e14_baseline / xuna_e14?)")
    return last, net, boots, reasons


def num(d, key):
    return int(d.get(key, "0"), 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--run", required=True)
    parser.add_argument("--down-for", type=int, default=10, help="e14_server.py --down-for")
    args = parser.parse_args()
    base, _, base_boots, base_reasons = parse(args.baseline)
    run, net, run_boots, run_reasons = parse(args.run)
    if not net:
        sys.exit(f"{args.run}: no 'e14_net' line (network run must use -e xuna_e14)")

    render_limit = num(base, "render_max_us") * 12 // 10
    reconnect_limit = 120_000 + args.down_for * 1000
    rows = [
        ("Internal heap minimum ≥ 40 KB", f"{num(base, 'heap_int_min')} B",
         f"{num(run, 'heap_int_min')} B", num(run, "heap_int_min") >= 40 * 1024),
        ("Loop gap p99 ≤ 50 ms", f"{num(base, 'gap_p99_us')} µs", f"{num(run, 'gap_p99_us')} µs",
         num(run, "gap_p99_us") <= 50_000),
        (f"Render max ≤ baseline + 20 % ({render_limit} µs)", f"{num(base, 'render_max_us')} µs",
         f"{num(run, 'render_max_us')} µs", num(run, "render_max_us") <= render_limit),
        ("BLE commands dropped = 0", str(num(base, "ble_dropped")), str(num(run, "ble_dropped")),
         num(run, "ble_dropped") == 0),
        ("Resets = 0 (one boot per log)", f"{base_boots} boot(s) {base_reasons}",
         f"{run_boots} boot(s) {run_reasons}", run_boots == 1 and base_boots == 1),
        (f"Reconnect ≤ 2 min after the server returns (≤ {reconnect_limit} ms incl. downtime)", "—",
         f"max {num(net, 'reconnect_ms_max')} ms, {num(net, 'disconnects')} disconnects, "
         f"state {num(net, 'state')}",
         num(net, "reconnect_ms_max") <= reconnect_limit and num(net, "state") == 2),
    ]
    info = [
        ("Run length", f"{num(base, 't_s')} s", f"{num(run, 't_s')} s"),
        ("Internal heap free / largest block (last)",
         f"{num(base, 'heap_int_free')} / {num(base, 'heap_int_largest')} B",
         f"{num(run, 'heap_int_free')} / {num(run, 'heap_int_largest')} B"),
        ("PSRAM free (last)", f"{num(base, 'psram_free')} B", f"{num(run, 'psram_free')} B"),
        ("Loop gap p50 / max", f"{num(base, 'gap_p50_us')} / {num(base, 'gap_max_us')} µs",
         f"{num(run, 'gap_p50_us')} / {num(run, 'gap_max_us')} µs"),
        ("Render mean", f"{num(base, 'render_mean_us')} µs", f"{num(run, 'render_mean_us')} µs"),
        ("BLE commands / not OK / board max µs",
         f"{num(base, 'ble_cmds')} / {num(base, 'ble_nonok')} / {num(base, 'ble_max_us')}",
         f"{num(run, 'ble_cmds')} / {num(run, 'ble_nonok')} / {num(run, 'ble_max_us')}"),
        ("TLS + WS connect last / max", "—",
         f"{num(net, 'connect_ms_last')} / {num(net, 'connect_ms_max')} ms"),
        ("Echo sent / ok / bad / lost / dropped", "—",
         f"{num(net, 'sent')} / {num(net, 'ok')} / {num(net, 'bad')} / {num(net, 'lost')} / "
         f"{num(net, 'dropped')}"),
        ("Echo RTT mean / max", "—", f"{num(net, 'rtt_ms_mean')} / {num(net, 'rtt_ms_max')} ms"),
        ("Wi-Fi joins / drops, RSSI", "—",
         f"{num(net, 'wifi_joins')} / {num(net, 'wifi_drops')}, {num(net, 'rssi')} dBm"),
        ("Network task stack free", "—", f"{num(net, 'stack_free')} B"),
    ]
    sys.stdout.reconfigure(encoding="utf-8")  # Windows consoles default to cp1252
    out = sys.stdout
    out.write("| Threshold | Baseline | E14 | Result |\n| --- | --- | --- | --- |\n")
    for name, b, r, ok in rows:
        out.write(f"| {name} | {b} | {r} | {'PASS' if ok else 'FAIL'} |\n")
    out.write("\n| Measure | Baseline | E14 |\n| --- | --- | --- |\n")
    for name, b, r in info:
        out.write(f"| {name} | {b} | {r} |\n")
    short = [n for n, d in (("baseline", base), ("run", run)) if num(d, "t_s") < 3600]
    if short:
        out.write(f"\nWARNING: shorter than 60 min: {', '.join(short)}\n")
    if num(run, "ble_nonok") > num(base, "ble_nonok"):
        out.write("\nCHECK: more not-OK BLE results than the baseline (busy/expired are normal; compare)\n")
    out.write("\nOwner checks by hand: no BLE error or timeout in the app; router power-cycle reconnect.\n")
    failed = [name for name, _, _, ok in rows if not ok]
    out.write(f"\n{'GATE PASS (automatic rows)' if not failed else 'GATE FAIL: ' + '; '.join(failed)}\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
