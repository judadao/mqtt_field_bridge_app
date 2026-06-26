#!/usr/bin/env bash
# Measure a 4-broker admission=2 layout where fallback should fill 2/2/2/2.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
STAMP=${STAMP:-$(date +%Y%m%d-%H%M%S)}
OUT_DIR="$ROOT_DIR/tests/linux/out/fair4_admission2/$STAMP"
RESULT_DOC="$ROOT_DIR/docs/load_balance_throughput_results.md"

DURATION=${DURATION:-20}
SETTLE=${SETTLE:-5}
PROPAGATE=${PROPAGATE:-4}
ADMISSION=${ADMISSION:-2}
PUBLISH_DELAY=${PUBLISH_DELAY:-0.0005}
PORT_BASE=${PORT_BASE:-32000}

mkdir -p "$OUT_DIR"

run_case() {
    impl=$1
    port_base=$2
    log="$OUT_DIR/fair4-${impl}.log"

    printf '\n=== fair4 / %s ===\n' "$impl" | tee -a "$OUT_DIR/fair4.log"
    "$ROOT_DIR/tests/linux/load_balance_throughput.py" \
        --mode fair4 \
        --impl "$impl" \
        --broker-count 4 \
        --duration "$DURATION" \
        --settle "$SETTLE" \
        --propagate "$PROPAGATE" \
        --admission "$ADMISSION" \
        --publish-delay "$PUBLISH_DELAY" \
        --port-base "$port_base" \
        >"$log" 2>&1

    python3 - "$log" "$OUT_DIR/fair4-${impl}.json" <<'PY'
import json
import sys
from pathlib import Path

log = Path(sys.argv[1]).read_text(encoding="utf-8")
start = log.find("{")
if start < 0:
    raise SystemExit(f"no JSON result in {sys.argv[1]}")
data = json.loads(log[start:])
Path(sys.argv[2]).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
traffic = data["traffic"]
connected = data["connected"]
rejected = data["rejected"]
fallback = data["fallback"]
print(
    f"fair4 {data['case']['impl']}: "
    f"throughput={traffic['throughput_msg_per_sec']} msg/s, "
    f"received={traffic['received_messages']}, "
    f"published={traffic['published_messages']}, "
    f"clients={connected['clients_by_broker']}, "
    f"subs={connected['subscribers_by_broker']}, "
    f"pubs={connected['publishers_by_broker']}, "
    f"rejected_subs={rejected['subscribers']}, "
    f"rejected_pubs={rejected['publishers']}, "
    f"fallback_subs={fallback['subscriber_fallback_count']}, "
    f"fallback_pubs={fallback['publisher_fallback_count']}"
)
PY
}

{
    printf 'Fair4 admission=2 throughput test started at %s\n' "$(date -Is)"
    printf 'duration=%s settle=%s propagate=%s admission=%s publish_delay=%s\n' \
        "$DURATION" "$SETTLE" "$PROPAGATE" "$ADMISSION" "$PUBLISH_DELAY"
} | tee "$OUT_DIR/fair4.log"

run_case mosquitto "$PORT_BASE" | tee -a "$OUT_DIR/fair4.log"
run_case field_no_fallback "$((PORT_BASE + 1000))" | tee -a "$OUT_DIR/fair4.log"
run_case field_fallback "$((PORT_BASE + 2000))" | tee -a "$OUT_DIR/fair4.log"

python3 - "$OUT_DIR" "$STAMP" "$DURATION" "$ADMISSION" "$RESULT_DOC" <<'PY'
import json
import sys
from pathlib import Path

out_dir = Path(sys.argv[1])
stamp = sys.argv[2]
duration = sys.argv[3]
admission = sys.argv[4]
result_doc = Path(sys.argv[5])

rows = []
for impl in ("mosquitto", "field_no_fallback", "field_fallback"):
    rows.append(json.loads((out_dir / f"fair4-{impl}.json").read_text(encoding="utf-8")))

summary = {
    "stamp": stamp,
    "duration_sec": int(duration),
    "admission": int(admission),
    "results": rows,
}
(out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

lines = []
lines.append(f"## {stamp} fair4 admission=2 throughput")
lines.append("")
lines.append(f"- Duration: {duration}s per case")
lines.append(f"- Field broker admission limit: {admission} clients per broker")
lines.append("- Workload: 4 publishers and 4 subscribers all initially target broker A.")
lines.append("- Expected fallback layout: 4 brokers x 2 clients = `2/2/2/2` total capacity.")
lines.append(f"- Artifacts: `{out_dir}`")
lines.append("")
lines.append("| Impl | Conn clients A/B/C/D | Conn subs A/B/C/D | Conn pubs A/B/C/D | Rej subs | Rej pubs | Fallback subs | Fallback pubs | Published | Received | Msg/s | Delivery % |")
lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
for data in rows:
    c = data["connected"]
    r = data["rejected"]
    f = data["fallback"]
    t = data["traffic"]
    lines.append(
        "| {impl} | {clients} | {subs} | {pubs} | {rej_s} | {rej_p} | {fb_s} | {fb_p} | {pubd} | {recv} | {rate} | {delivery} |".format(
            impl=data["case"]["impl"],
            clients="/".join(str(x) for x in c["clients_by_broker"]),
            subs="/".join(str(x) for x in c["subscribers_by_broker"]),
            pubs="/".join(str(x) for x in c["publishers_by_broker"]),
            rej_s=r["subscribers"],
            rej_p=r["publishers"],
            fb_s=f["subscriber_fallback_count"],
            fb_p=f["publisher_fallback_count"],
            pubd=t["published_messages"],
            recv=t["received_messages"],
            rate=t["throughput_msg_per_sec"],
            delivery=t["delivery_rate_percent"],
        )
    )
lines.append("")

text = "\n".join(lines)
(out_dir / "summary.md").write_text(text + "\n", encoding="utf-8")

if result_doc.exists():
    previous = result_doc.read_text(encoding="utf-8")
else:
    previous = "# Load Balance Throughput Results\n\n"
result_doc.write_text(previous.rstrip() + "\n\n" + text + "\n", encoding="utf-8")
print(text)
PY

printf '\nDONE: %s\n' "$OUT_DIR" | tee -a "$OUT_DIR/fair4.log"
