#!/usr/bin/env bash
# Show dynamic fallback advantage when a hot broker receives a burst of clients.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
STAMP=${STAMP:-$(date +%Y%m%d-%H%M%S)}
OUT_DIR="$ROOT_DIR/tests/linux/out/dynamic_balance_burst/$STAMP"
RESULT_DOC="$ROOT_DIR/docs/load_balance_throughput_results.md"

DURATION=${DURATION:-20}
SETTLE=${SETTLE:-5}
PROPAGATE=${PROPAGATE:-4}
ADMISSION=${ADMISSION:-8}
PUBLISH_DELAY=${PUBLISH_DELAY:-0.0005}
TOPIC_COUNT=${TOPIC_COUNT:-1}
PORT_BASE=${PORT_BASE:-41000}

mkdir -p "$OUT_DIR"

run_case() {
    impl=$1
    port_base=$2
    log="$OUT_DIR/dynamic-${impl}.log"

    printf '\n=== dynamic_burst / %s ===\n' "$impl" | tee -a "$OUT_DIR/dynamic.log"
    "$ROOT_DIR/tests/linux/load_balance_throughput.py" \
        --mode dynamic_burst \
        --impl "$impl" \
        --broker-count 4 \
        --duration "$DURATION" \
        --settle "$SETTLE" \
        --propagate "$PROPAGATE" \
        --admission "$ADMISSION" \
        --publish-delay "$PUBLISH_DELAY" \
        --topic-count "$TOPIC_COUNT" \
        --port-base "$port_base" \
        >"$log" 2>&1

    python3 - "$log" "$OUT_DIR/dynamic-${impl}.json" <<'PY'
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
    f"dynamic {data['case']['impl']}: "
    f"throughput={traffic['throughput_msg_per_sec']} msg/s, "
    f"topics={data['case']['topic_count']}, "
    f"topic_subs={connected['topic_subscriptions']}, "
    f"topics_by_broker={connected['topics_by_broker']}, "
    f"clients={connected['clients_by_broker']}, "
    f"subs={connected['subscribers_by_broker']}, "
    f"pubs={connected['publishers_by_broker']}, "
    f"rejected_subs={rejected['subscribers']}, "
    f"fallback_subs={fallback['subscriber_fallback_count']}, "
    f"received={traffic['received_messages']}, "
    f"delivery={traffic['delivery_rate_percent']}%"
)
PY
}

{
    printf 'Dynamic balance burst test started at %s\n' "$(date -Is)"
    printf 'duration=%s settle=%s propagate=%s admission=%s publish_delay=%s topic_count=%s\n' \
        "$DURATION" "$SETTLE" "$PROPAGATE" "$ADMISSION" "$PUBLISH_DELAY" "$TOPIC_COUNT"
    printf 'preload clients: A=8 B=2 C=2 D=2; burst subscribers targeting A=18\n'
} | tee "$OUT_DIR/dynamic.log"

run_case field_no_fallback "$PORT_BASE" | tee -a "$OUT_DIR/dynamic.log"
run_case field_fallback "$((PORT_BASE + 1000))" | tee -a "$OUT_DIR/dynamic.log"

python3 - "$OUT_DIR" "$STAMP" "$DURATION" "$ADMISSION" "$TOPIC_COUNT" "$RESULT_DOC" <<'PY'
import json
import sys
from pathlib import Path

out_dir = Path(sys.argv[1])
stamp = sys.argv[2]
duration = sys.argv[3]
admission = sys.argv[4]
topic_count = sys.argv[5]
result_doc = Path(sys.argv[6])

rows = []
for impl in ("field_no_fallback", "field_fallback"):
    rows.append(json.loads((out_dir / f"dynamic-{impl}.json").read_text(encoding="utf-8")))

summary = {
    "stamp": stamp,
    "duration_sec": int(duration),
    "admission": int(admission),
    "topic_count": int(topic_count),
    "results": rows,
}
(out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

lines = []
lines.append(f"## {stamp} dynamic balance burst")
lines.append("")
lines.append(f"- Duration: {duration}s per case")
lines.append(f"- Field broker admission limit: {admission} clients per broker")
lines.append(f"- Topic count: {topic_count}")
lines.append("- Preload: broker A has 7 subscribers + 1 publisher; B/C/D have 2 subscribers each.")
lines.append("- Burst: 18 new subscribers all try broker A first.")
lines.append(f"- Artifacts: `{out_dir}`")
lines.append("")
lines.append("| Impl | Topics | Topic subs | Topics A/B/C/D | Conn clients A/B/C/D | Conn subs A/B/C/D | Rej subs | Fallback subs | Published | Received | Msg/s | Delivery % |")
lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
for data in rows:
    c = data["connected"]
    r = data["rejected"]
    f = data["fallback"]
    t = data["traffic"]
    lines.append(
        "| {impl} | {topics} | {topic_subs} | {topics_by_broker} | {clients} | {subs} | {rej_s} | {fb_s} | {pubd} | {recv} | {rate} | {delivery} |".format(
            impl=data["case"]["impl"],
            topics=data["case"]["topic_count"],
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

printf '\nDONE: %s\n' "$OUT_DIR" | tee -a "$OUT_DIR/dynamic.log"
