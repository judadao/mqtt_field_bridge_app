#!/usr/bin/env bash
# Run the load-balance throughput matrix and record comparable results.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
STAMP=${STAMP:-$(date +%Y%m%d-%H%M%S)}
OUT_DIR="$ROOT_DIR/tests/linux/out/load_balance_matrix/$STAMP"
RESULT_DOC="$ROOT_DIR/docs/load_balance_throughput_results.md"

DURATION=${DURATION:-20}
SETTLE=${SETTLE:-5}
PROPAGATE=${PROPAGATE:-4}
ADMISSION=${ADMISSION:-12}
PUBLISH_DELAY=${PUBLISH_DELAY:-0.0005}
PORT_BASE=${PORT_BASE:-25000}

mkdir -p "$OUT_DIR"

run_case() {
    mode=$1
    impl=$2
    port_base=$3
    log="$OUT_DIR/${mode}-${impl}.log"

    printf '\n=== %s / %s ===\n' "$mode" "$impl" | tee -a "$OUT_DIR/matrix.log"
    "$ROOT_DIR/tests/linux/load_balance_throughput.py" \
        --mode "$mode" \
        --impl "$impl" \
        --duration "$DURATION" \
        --settle "$SETTLE" \
        --propagate "$PROPAGATE" \
        --admission "$ADMISSION" \
        --publish-delay "$PUBLISH_DELAY" \
        --port-base "$port_base" \
        >"$log" 2>&1

    python3 - "$log" "$OUT_DIR/${mode}-${impl}.json" <<'PY'
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
    f"{data['case']['mode']} {data['case']['impl']}: "
    f"throughput={traffic['throughput_msg_per_sec']} msg/s, "
    f"received={traffic['received_messages']}, "
    f"published={traffic['published_messages']}, "
    f"subs={connected['subscribers_by_broker']}, "
    f"pubs={connected['publishers_by_broker']}, "
    f"rejected_subs={rejected['subscribers']}, "
    f"fallback_subs={fallback['subscriber_fallback_count']}"
)
PY
}

{
    printf 'Load-balance throughput matrix started at %s\n' "$(date -Is)"
    printf 'duration=%s settle=%s propagate=%s admission=%s publish_delay=%s\n' \
        "$DURATION" "$SETTLE" "$PROPAGATE" "$ADMISSION" "$PUBLISH_DELAY"
} | tee "$OUT_DIR/matrix.log"

case_index=0
for mode in hotspot uneven; do
    for impl in mosquitto field_no_fallback field_fallback; do
        run_case "$mode" "$impl" "$((PORT_BASE + case_index * 1000))" | tee -a "$OUT_DIR/matrix.log"
        case_index=$((case_index + 1))
    done
done

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
for mode in ("hotspot", "uneven"):
    for impl in ("mosquitto", "field_no_fallback", "field_fallback"):
        data = json.loads((out_dir / f"{mode}-{impl}.json").read_text(encoding="utf-8"))
        rows.append(data)

summary = {
    "stamp": stamp,
    "duration_sec": int(duration),
    "admission": int(admission),
    "results": rows,
}
(out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

lines = []
lines.append(f"## {stamp} load-balance throughput matrix")
lines.append("")
lines.append(f"- Duration: {duration}s per case")
lines.append(f"- Field broker admission limit: {admission} clients per broker")
lines.append(f"- Artifacts: `{out_dir}`")
lines.append("- Note: mosquitto has no bridge or fallback; uneven mosquitto is aggregate local broker traffic.")
lines.append("")
lines.append("| Mode | Impl | Conn subs A/B/C | Conn pubs A/B/C | Rej subs | Fallback subs | Published | Received | Msg/s | Delivery % |")
lines.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|")
for data in rows:
    c = data["connected"]
    r = data["rejected"]
    f = data["fallback"]
    t = data["traffic"]
    lines.append(
        "| {mode} | {impl} | {subs} | {pubs} | {rej} | {fb} | {pubd} | {recv} | {rate} | {delivery} |".format(
            mode=data["case"]["mode"],
            impl=data["case"]["impl"],
            subs="/".join(str(x) for x in c["subscribers_by_broker"]),
            pubs="/".join(str(x) for x in c["publishers_by_broker"]),
            rej=r["subscribers"],
            fb=f["subscriber_fallback_count"],
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

printf '\nDONE: %s\n' "$OUT_DIR" | tee -a "$OUT_DIR/matrix.log"
