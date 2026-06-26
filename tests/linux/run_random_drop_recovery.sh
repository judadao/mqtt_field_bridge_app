#!/usr/bin/env bash
# Show fallback throughput advantage when random peer brokers are unavailable.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
STAMP=${STAMP:-$(date +%Y%m%d-%H%M%S)}
OUT_DIR="$ROOT_DIR/tests/linux/out/random_drop_recovery/$STAMP"
RESULT_DOC="$ROOT_DIR/docs/load_balance_throughput_results.md"

DURATION=${DURATION:-20}
SETTLE=${SETTLE:-5}
PROPAGATE=${PROPAGATE:-4}
ADMISSION=${ADMISSION:-32}
PUBLISH_DELAY=${PUBLISH_DELAY:-0.0005}
TOPIC_COUNT=${TOPIC_COUNT:-16}
PORT_BASE=${PORT_BASE:-43000}
DROP_COUNT=${DROP_COUNT:-2}
DROP_SEED=${DROP_SEED:-260626}
RANDOM_DROP_SUBSCRIBERS=${RANDOM_DROP_SUBSCRIBERS:-4,8,8,8}

mkdir -p "$OUT_DIR"

run_case() {
    impl=$1
    port_base=$2
    log="$OUT_DIR/random-drop-${impl}.log"

    printf '\n=== random_drop / %s ===\n' "$impl" | tee -a "$OUT_DIR/random-drop.log"
    "$ROOT_DIR/tests/linux/load_balance_throughput.py" \
        --mode random_drop \
        --impl "$impl" \
        --broker-count 4 \
        --duration "$DURATION" \
        --settle "$SETTLE" \
        --propagate "$PROPAGATE" \
        --admission "$ADMISSION" \
        --publish-delay "$PUBLISH_DELAY" \
        --topic-count "$TOPIC_COUNT" \
        --port-base "$port_base" \
        --drop-count "$DROP_COUNT" \
        --drop-seed "$DROP_SEED" \
        --random-drop-subscribers "$RANDOM_DROP_SUBSCRIBERS" \
        >"$log" 2>&1

    python3 - "$log" "$OUT_DIR/random-drop-${impl}.json" <<'PY'
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
failure = data["failure"]
print(
    f"random_drop {data['case']['impl']}: "
    f"dropped={failure['dropped_brokers']}, "
    f"throughput={traffic['throughput_msg_per_sec']} msg/s, "
    f"topics={data['case']['topic_count']}, "
    f"clients={connected['clients_by_broker']}, "
    f"subs={connected['subscribers_by_broker']}, "
    f"rejected_subs={rejected['subscribers']}, "
    f"fallback_subs={fallback['subscriber_fallback_count']}, "
    f"received={traffic['received_messages']}, "
    f"delivery={traffic['delivery_rate_percent']}%"
)
PY
}

{
    printf 'Random broker drop recovery test started at %s\n' "$(date -Is)"
    printf 'duration=%s settle=%s propagate=%s admission=%s publish_delay=%s topic_count=%s\n' \
        "$DURATION" "$SETTLE" "$PROPAGATE" "$ADMISSION" "$PUBLISH_DELAY" "$TOPIC_COUNT"
    printf 'drop_count=%s drop_seed=%s subscribers=%s\n' \
        "$DROP_COUNT" "$DROP_SEED" "$RANDOM_DROP_SUBSCRIBERS"
} | tee "$OUT_DIR/random-drop.log"

run_case field_no_fallback "$PORT_BASE" | tee -a "$OUT_DIR/random-drop.log"
run_case field_fallback "$((PORT_BASE + 1000))" | tee -a "$OUT_DIR/random-drop.log"

python3 - "$OUT_DIR" "$STAMP" "$DURATION" "$ADMISSION" "$TOPIC_COUNT" "$DROP_COUNT" "$DROP_SEED" "$RESULT_DOC" <<'PY'
import json
import sys
from pathlib import Path

out_dir = Path(sys.argv[1])
stamp = sys.argv[2]
duration = sys.argv[3]
admission = sys.argv[4]
topic_count = sys.argv[5]
drop_count = sys.argv[6]
drop_seed = sys.argv[7]
result_doc = Path(sys.argv[8])

rows = []
for impl in ("field_no_fallback", "field_fallback"):
    rows.append(json.loads((out_dir / f"random-drop-{impl}.json").read_text(encoding="utf-8")))

summary = {
    "stamp": stamp,
    "duration_sec": int(duration),
    "admission": int(admission),
    "topic_count": int(topic_count),
    "drop_count": int(drop_count),
    "drop_seed": int(drop_seed),
    "results": rows,
}
(out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

lines = []
lines.append(f"## {stamp} random broker drop recovery")
lines.append("")
lines.append(f"- Duration: {duration}s per case")
lines.append(f"- Field broker admission limit: {admission} clients per broker")
lines.append(f"- Topic count: {topic_count}")
lines.append(f"- Random drop: {drop_count} non-publisher broker(s), seed `{drop_seed}`")
lines.append("- Workload: one publisher remains on broker A; subscribers initially target A/B/C/D.")
lines.append(f"- Artifacts: `{out_dir}`")
lines.append("")
lines.append("| Impl | Dropped brokers | Topic subs | Topics A/B/C/D | Conn clients A/B/C/D | Conn subs A/B/C/D | Rej subs | Fallback subs | Published | Received | Msg/s | Delivery % |")
lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
for data in rows:
    c = data["connected"]
    r = data["rejected"]
    f = data["fallback"]
    t = data["traffic"]
    failure = data["failure"]
    lines.append(
        "| {impl} | {dropped} | {topic_subs} | {topics_by_broker} | {clients} | {subs} | {rej_s} | {fb_s} | {pubd} | {recv} | {rate} | {delivery} |".format(
            impl=data["case"]["impl"],
            dropped="/".join(str(x) for x in failure["dropped_brokers"]),
            topic_subs=c["topic_subscriptions"],
            topics_by_broker="/".join(str(x) for x in c["topics_by_broker"]),
            clients="/".join(str(x) for x in c["clients_by_broker"]),
            subs="/".join(str(x) for x in c["subscribers_by_broker"]),
            rej_s=r["subscribers"],
            fb_s=f["subscriber_fallback_count"],
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

printf '\nDONE: %s\n' "$OUT_DIR" | tee -a "$OUT_DIR/random-drop.log"
