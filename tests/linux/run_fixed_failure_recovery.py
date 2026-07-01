#!/usr/bin/env python3
"""Fixed-message broker failure recovery benchmark.

Each intended broker owns one publisher and one subscriber on a local topic.
Publishers attempt to send a fixed number of messages, then one random broker is
terminated during the run. Fallback-enabled clients reconnect to a live broker
and continue the remaining publish/receive workload.
"""

from __future__ import annotations

import json
import os
import random
import subprocess
import threading
import time
from pathlib import Path

from load_balance_throughput import (
    BROKER_DIR,
    MqttClient,
    build_field_binary,
    start_mosquitto,
    stop_broker,
    stop_ports,
    wait_port,
)


ROOT = Path(__file__).resolve().parents[2]
OUT_ROOT = ROOT / "tests" / "linux" / "out" / "fixed_failure_recovery"
RESULT_DOC = ROOT / "docs" / "load_balance_throughput_results.md"


BROKER_COUNT = int(os.environ.get("BROKER_COUNT", "4"))
MESSAGES_PER_BROKER = int(os.environ.get("MESSAGES_PER_BROKER", "10000"))
DROP_SEED = int(os.environ.get("DROP_SEED", "260626"))
DROP_COUNT = os.environ.get("DROP_COUNT", "")
DROP_BROKERS = os.environ.get("DROP_BROKERS", "")
DROP_AFTER_SEC = float(os.environ.get("DROP_AFTER_SEC", "0.5"))
DROP_HOLD_SEC = float(os.environ.get("DROP_HOLD_SEC", "3.0"))
PUBLISH_DELAY = float(os.environ.get("PUBLISH_DELAY", "0.0002"))
TIMEOUT_SEC = float(os.environ.get("TIMEOUT_SEC", "30.0"))
PORT_BASE = int(os.environ.get("PORT_BASE", "47000"))
ADMISSION = int(os.environ.get("ADMISSION", "64"))
STAMP = os.environ.get("STAMP", time.strftime("%Y%m%d-%H%M%S"))


def broker_name(idx: int) -> str:
    if 0 <= idx < 26:
        return chr(ord("A") + idx)
    return str(idx)


def broker_names(indices: list[int]) -> str:
    return "/".join(broker_name(idx) for idx in indices)


def parse_dropped_brokers() -> list[int]:
    if DROP_BROKERS:
        brokers = [int(value.strip()) for value in DROP_BROKERS.split(",") if value.strip()]
    else:
        rng = random.Random(DROP_SEED)
        drop_count = int(DROP_COUNT) if DROP_COUNT else rng.randint(1, BROKER_COUNT - 1)
        if drop_count < 1 or drop_count >= BROKER_COUNT:
            raise SystemExit(f"DROP_COUNT must be between 1 and {BROKER_COUNT - 1}")
        candidates = list(range(BROKER_COUNT))
        rng.shuffle(candidates)
        brokers = sorted(candidates[:drop_count])
    if not brokers:
        raise SystemExit("at least one broker must be dropped")
    if len(set(brokers)) != len(brokers):
        raise SystemExit("dropped broker list contains duplicates")
    for broker_idx in brokers:
        if broker_idx < 0 or broker_idx >= BROKER_COUNT:
            raise SystemExit(f"dropped broker must be between 0 and {BROKER_COUNT - 1}")
    return sorted(brokers)


class FixedSubscriber:
    def __init__(self, intended: int, ports: list[int], fallback: bool):
        self.intended = intended
        self.ports = ports
        self.fallback = fallback
        self.topic = f"site/fixed-failure/broker-{intended}"
        self.connected = -1
        self.messages: set[int] = set()
        self.reconnects = 0
        self.stop = threading.Event()
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def _connect(self) -> MqttClient | None:
        order = list(range(len(self.ports))) if self.fallback else [self.intended]
        if self.fallback:
            order = order[self.intended :] + order[: self.intended]
        while not self.stop.is_set():
            for broker_idx in order:
                try:
                    client = MqttClient(
                        f"fs{self.intended}b{broker_idx}",
                        "127.0.0.1",
                        self.ports[broker_idx],
                    )
                    if not client.subscribe(self.topic):
                        client.close()
                        continue
                    if self.connected != -1 and self.connected != broker_idx:
                        self.reconnects += 1
                    self.connected = broker_idx
                    self.ready.set()
                    return client
                except OSError:
                    continue
            if not self.fallback:
                return None
            time.sleep(0.05)
        return None

    def _run(self) -> None:
        while not self.stop.is_set():
            client = self._connect()
            if client is None:
                return
            while not self.stop.is_set():
                try:
                    payload = client.recv_publish_payload()
                except OSError:
                    client.close()
                    if not self.fallback:
                        return
                    break
                if payload is None:
                    continue
                try:
                    broker_s, seq_s = payload.split(":", 1)
                    if int(broker_s) == self.intended:
                        self.messages.add(int(seq_s))
                except ValueError:
                    continue

    def close(self) -> None:
        self.stop.set()
        self.thread.join(timeout=1.0)


class FixedPublisher:
    def __init__(self, intended: int, ports: list[int], fallback: bool):
        self.intended = intended
        self.ports = ports
        self.fallback = fallback
        self.topic = f"site/fixed-failure/broker-{intended}"
        self.connected = -1
        self.sent = 0
        self.errors = 0
        self.reconnects = 0
        self.completed = False
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def _connect(self) -> MqttClient | None:
        order = list(range(len(self.ports))) if self.fallback else [self.intended]
        if self.fallback:
            order = order[self.intended :] + order[: self.intended]
        while not self.stop.is_set():
            for broker_idx in order:
                try:
                    client = MqttClient(
                        f"fp{self.intended}b{broker_idx}",
                        "127.0.0.1",
                        self.ports[broker_idx],
                    )
                    if self.connected != -1 and self.connected != broker_idx:
                        self.reconnects += 1
                    self.connected = broker_idx
                    return client
                except OSError:
                    continue
            if not self.fallback:
                return None
            time.sleep(0.05)
        return None

    def _run(self) -> None:
        client = self._connect()
        if client is None:
            return
        while not self.stop.is_set() and self.sent < MESSAGES_PER_BROKER:
            try:
                client.publish(self.topic, f"{self.intended}:{self.sent}")
                self.sent += 1
                if PUBLISH_DELAY > 0:
                    time.sleep(PUBLISH_DELAY)
            except OSError:
                self.errors += 1
                client.close()
                if not self.fallback:
                    return
                client = self._connect()
                if client is None:
                    return
                time.sleep(0.2)
        self.completed = self.sent >= MESSAGES_PER_BROKER
        client.close()

    def close(self) -> None:
        self.stop.set()
        self.thread.join(timeout=1.0)


def start_field_fixed(outdir: Path, ports: list[int], p2p_ports: list[int]) -> list[subprocess.Popen]:
    disc_port = ports[0] + 4000
    bins = []
    for idx, port in enumerate(ports):
        binary = outdir / f"field_b{idx + 1}"
        build_field_binary(binary, port, p2p_ports[idx], disc_port, ADMISSION, 512)
        bins.append(binary)

    procs = []
    for idx, binary in enumerate(bins):
        peers = [f"127.0.0.1:{p}" for j, p in enumerate(p2p_ports) if j != idx]
        env = os.environ.copy()
        env["MQTT_P2P_PEERS"] = ",".join(peers)
        log = open(outdir / f"field_b{idx + 1}.log", "w", encoding="utf-8")
        procs.append(subprocess.Popen([str(binary)], stdout=log, stderr=log, env=env))
    for port in ports:
        wait_port(port)
    return procs


def restart_mosquitto(case_dir: Path, broker_idx: int, port: int) -> subprocess.Popen:
    conf = case_dir / f"mosquitto_{broker_idx}.conf"
    log = open(case_dir / f"mosquitto_{broker_idx}_restart.log", "w", encoding="utf-8")
    proc = subprocess.Popen(["mosquitto", "-c", str(conf)], stdout=log, stderr=log)
    wait_port(port)
    return proc


def restart_field(case_dir: Path, broker_idx: int, p2p_ports: list[int], port: int) -> subprocess.Popen:
    binary = case_dir / f"field_b{broker_idx + 1}"
    peers = [f"127.0.0.1:{p}" for j, p in enumerate(p2p_ports) if j != broker_idx]
    env = os.environ.copy()
    env["MQTT_P2P_PEERS"] = ",".join(peers)
    log = open(case_dir / f"field_b{broker_idx + 1}_restart.log", "w", encoding="utf-8")
    proc = subprocess.Popen([str(binary)], stdout=log, stderr=log, env=env)
    wait_port(port)
    return proc


def run_case(impl: str, port_base: int, dropped_brokers: list[int], outdir: Path) -> dict:
    case_dir = outdir / impl
    case_dir.mkdir(parents=True, exist_ok=True)
    if impl == "mosquitto":
        ports = [port_base + i for i in range(BROKER_COUNT)]
        p2p_ports: list[int] = []
        fallback = False
    else:
        ports = [port_base + 100 + i for i in range(BROKER_COUNT)]
        p2p_ports = [port_base + 300 + i for i in range(BROKER_COUNT)]
        fallback = impl == "field_fallback"
    stop_ports(ports + p2p_ports)
    if impl == "mosquitto":
        procs = start_mosquitto(case_dir, ports)
    else:
        procs = start_field_fixed(case_dir, ports, p2p_ports)

    subscribers = [FixedSubscriber(idx, ports, fallback) for idx in range(BROKER_COUNT)]
    publishers = [FixedPublisher(idx, ports, fallback) for idx in range(BROKER_COUNT)]

    failure_done = threading.Event()

    def fail_brokers() -> None:
        time.sleep(DROP_AFTER_SEC)
        for dropped in dropped_brokers:
            stop_broker(procs[dropped])
        time.sleep(DROP_HOLD_SEC)
        for dropped in dropped_brokers:
            if impl == "mosquitto":
                procs[dropped] = restart_mosquitto(case_dir, dropped, ports[dropped])
            else:
                procs[dropped] = restart_field(case_dir, dropped, p2p_ports, ports[dropped])
        failure_done.set()

    started = time.time()
    try:
        for sub in subscribers:
            sub.start()
        deadline = time.time() + 5.0
        while time.time() < deadline and not all(sub.ready.is_set() for sub in subscribers):
            time.sleep(0.05)
        for pub in publishers:
            pub.start()
        failure_thread = threading.Thread(target=fail_brokers, daemon=True)
        failure_thread.start()

        deadline = time.time() + TIMEOUT_SEC
        while time.time() < deadline:
            if all(
                pub.completed
                or (not fallback and pub.intended in dropped_brokers and pub.sent < MESSAGES_PER_BROKER)
                for pub in publishers
            ):
                break
            time.sleep(0.05)
        for pub in publishers:
            pub.close()
        time.sleep(1.0)
    finally:
        for sub in subscribers:
            sub.close()
        for proc in procs:
            if proc.poll() is None:
                stop_broker(proc)

    received_by_broker = [len(sub.messages) for sub in subscribers]
    sent_by_broker = [pub.sent for pub in publishers]
    completed_by_broker = [pub.completed for pub in publishers]
    expected_by_broker = [MESSAGES_PER_BROKER] * BROKER_COUNT
    expected = MESSAGES_PER_BROKER * BROKER_COUNT
    received = sum(received_by_broker)
    dropped_expected = MESSAGES_PER_BROKER * len(dropped_brokers)
    dropped_received = sum(received_by_broker[idx] for idx in dropped_brokers)
    return {
        "impl": impl,
        "dropped_brokers": dropped_brokers,
        "dropped_broker_names": [broker_name(idx) for idx in dropped_brokers],
        "messages_per_broker": MESSAGES_PER_BROKER,
        "expected_by_broker": expected_by_broker,
        "sent_by_broker": sent_by_broker,
        "received_by_broker": received_by_broker,
        "dropped_workload_received": dropped_received,
        "dropped_workload_expected": dropped_expected,
        "dropped_workload_delivery_percent": round(dropped_received * 100.0 / dropped_expected, 4),
        "publisher_completed_by_broker": completed_by_broker,
        "publisher_reconnects_by_broker": [pub.reconnects for pub in publishers],
        "subscriber_reconnects_by_broker": [sub.reconnects for sub in subscribers],
        "publisher_errors_by_broker": [pub.errors for pub in publishers],
        "expected_messages": expected,
        "received_messages": received,
        "missing_messages": expected - received,
        "delivery_percent": round(received * 100.0 / expected, 4),
        "elapsed_sec": round(time.time() - started, 3),
        "failure": {
            "drop_after_sec": DROP_AFTER_SEC,
            "drop_hold_sec": DROP_HOLD_SEC,
            "failure_window_elapsed": failure_done.is_set(),
        },
    }


def format_counts(values: list[int | bool]) -> str:
    return "/".join("1" if value is True else "0" if value is False else str(value) for value in values)


def broker_axis_label() -> str:
    return "/".join(broker_name(idx) for idx in range(BROKER_COUNT))


def main() -> int:
    outdir = OUT_ROOT / STAMP
    outdir.mkdir(parents=True, exist_ok=True)
    dropped_brokers = parse_dropped_brokers()
    axis = broker_axis_label()
    rows = [
        run_case("mosquitto", PORT_BASE, dropped_brokers, outdir),
        run_case("field_no_fallback", PORT_BASE + 1000, dropped_brokers, outdir),
        run_case("field_fallback", PORT_BASE + 2000, dropped_brokers, outdir),
    ]
    summary = {
        "stamp": STAMP,
        "broker_count": BROKER_COUNT,
        "messages_per_broker": MESSAGES_PER_BROKER,
        "expected_messages": BROKER_COUNT * MESSAGES_PER_BROKER,
        "drop_seed": DROP_SEED,
        "drop_count": len(dropped_brokers),
        "drop_brokers_override": DROP_BROKERS,
        "dropped_brokers": dropped_brokers,
        "dropped_broker_names": [broker_name(idx) for idx in dropped_brokers],
        "drop_after_sec": DROP_AFTER_SEC,
        "drop_hold_sec": DROP_HOLD_SEC,
        "publish_delay": PUBLISH_DELAY,
        "results": rows,
    }
    (outdir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    lines = [
        f"## {STAMP} fixed-message broker failure recovery",
        "",
        f"- Workload: {BROKER_COUNT} brokers, {MESSAGES_PER_BROKER} messages per broker, expected total {BROKER_COUNT * MESSAGES_PER_BROKER}.",
        f"- Failure: broker(s) {broker_names(dropped_brokers)} are terminated {DROP_AFTER_SEC}s after publishing starts, held down for {DROP_HOLD_SEC}s, then restarted.",
        "- Metric: received unique payloads versus the fixed expected message count; dropped workload isolates the failed broker workload.",
        f"- Artifacts: `{outdir}`",
        "",
        f"| Impl | Dropped | Elapsed sec | Expected {axis} | Sent {axis} | Received {axis} | Dropped workload | Dropped delivery % | Pub done {axis} | Pub reconnects | Sub reconnects | Missing | Delivery % |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            "| {impl} | {dropped} | {elapsed} | {expected} | {sent} | {received} | {dropped_workload} | {dropped_delivery} | {done} | {pub_rec} | {sub_rec} | {missing} | {delivery} |".format(
                impl=row["impl"],
                dropped="/".join(row["dropped_broker_names"]),
                elapsed=row["elapsed_sec"],
                expected=format_counts(row["expected_by_broker"]),
                sent=format_counts(row["sent_by_broker"]),
                received=format_counts(row["received_by_broker"]),
                dropped_workload=f"{row['dropped_workload_received']}/{row['dropped_workload_expected']}",
                dropped_delivery=row["dropped_workload_delivery_percent"],
                done=format_counts(row["publisher_completed_by_broker"]),
                pub_rec=format_counts(row["publisher_reconnects_by_broker"]),
                sub_rec=format_counts(row["subscriber_reconnects_by_broker"]),
                missing=row["missing_messages"],
                delivery=row["delivery_percent"],
            )
        )
    lines.append("")
    text = "\n".join(lines)
    (outdir / "summary.md").write_text(text + "\n", encoding="utf-8")
    previous = RESULT_DOC.read_text(encoding="utf-8") if RESULT_DOC.exists() else "# Load Balance Throughput Results\n"
    RESULT_DOC.write_text(previous.rstrip() + "\n\n" + text, encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
