#!/usr/bin/env python3
"""Load-balance throughput comparison for mosquitto and field bridge brokers."""

from __future__ import annotations

import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BROKER_DIR = ROOT / "deps" / "mqtt_min_broker"
OUT_ROOT = ROOT / "tests" / "linux" / "out" / "load_balance_throughput"


def enc_str(value: str) -> bytes:
    data = value.encode("utf-8")
    return struct.pack("!H", len(data)) + data


def enc_rem_len(value: int) -> bytes:
    out = bytearray()
    while True:
        byte = value % 128
        value //= 128
        if value:
            byte |= 0x80
        out.append(byte)
        if not value:
            return bytes(out)


def read_exact(sock: socket.socket, n: int) -> bytes:
    data = bytearray()
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise OSError("socket closed")
        data.extend(chunk)
    return bytes(data)


def read_packet(sock: socket.socket) -> tuple[int, bytes]:
    first = read_exact(sock, 1)[0]
    multiplier = 1
    remaining = 0
    while True:
        b = read_exact(sock, 1)[0]
        remaining += (b & 127) * multiplier
        if (b & 128) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise OSError("bad remaining length")
    return first, read_exact(sock, remaining)


class MqttClient:
    def __init__(self, client_id: str, host: str, port: int, timeout: float = 3.0):
        self.client_id = client_id[:23]
        self.host = host
        self.port = port
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self._connect()

    def _connect(self) -> None:
        variable = enc_str("MQTT") + bytes([4, 2]) + struct.pack("!H", 60)
        payload = enc_str(self.client_id)
        packet = bytes([0x10]) + enc_rem_len(len(variable) + len(payload)) + variable + payload
        self.sock.sendall(packet)
        typ, body = read_packet(self.sock)
        if typ != 0x20 or len(body) != 2:
            raise OSError("bad CONNACK")
        if body[1] != 0:
            raise ConnectionRefusedError(f"CONNACK rc={body[1]}")
        self.sock.settimeout(0.5)

    def subscribe(self, topic: str) -> bool:
        variable = struct.pack("!H", 1)
        payload = enc_str(topic) + bytes([0])
        packet = bytes([0x82]) + enc_rem_len(len(variable) + len(payload)) + variable + payload
        self.sock.sendall(packet)
        end = time.time() + 3.0
        while time.time() < end:
            typ, body = read_packet(self.sock)
            if typ == 0x90 and len(body) >= 3 and body[0:2] == b"\x00\x01":
                return body[2] != 0x80
        raise TimeoutError("SUBACK timeout")

    def publish(self, topic: str, payload: str) -> None:
        body = enc_str(topic) + payload.encode("utf-8")
        packet = bytes([0x30]) + enc_rem_len(len(body)) + body
        self.sock.sendall(packet)

    def recv_publish_payload(self) -> str | None:
        try:
            typ, body = read_packet(self.sock)
        except socket.timeout:
            return None
        if typ >> 4 != 3 or len(body) < 2:
            return None
        topic_len = struct.unpack("!H", body[:2])[0]
        payload = body[2 + topic_len :]
        return payload.decode("utf-8", errors="replace")

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


class Subscriber:
    def __init__(self, client: MqttClient, intended: int, connected: int, topic: str):
        self.client = client
        self.intended = intended
        self.connected = connected
        self.topic = topic
        self.received = 0
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def _run(self) -> None:
        while not self.stop.is_set():
            try:
                payload = self.client.recv_publish_payload()
            except OSError:
                if not self.stop.is_set():
                    pass
                break
            if payload is not None:
                self.received += 1

    def close(self) -> None:
        self.stop.set()
        self.client.close()
        self.thread.join(timeout=1.0)


class Publisher:
    def __init__(self, client: MqttClient, intended: int, connected: int, publish_delay: float):
        self.client = client
        self.intended = intended
        self.connected = connected
        self.publish_delay = publish_delay
        self.sent = 0
        self.sent_by_topic: dict[str, int] = {}
        self.errors = 0
        self.stop = threading.Event()
        self.thread: threading.Thread | None = None

    def start(self, topics: list[str]) -> None:
        self.thread = threading.Thread(target=self._run, args=(topics,), daemon=True)
        self.thread.start()

    def _run(self, topics: list[str]) -> None:
        seq = 0
        while not self.stop.is_set():
            try:
                topic = topics[seq % len(topics)]
                self.client.publish(topic, f"{self.client.client_id}-{seq}")
                self.sent += 1
                self.sent_by_topic[topic] = self.sent_by_topic.get(topic, 0) + 1
                seq += 1
                if self.publish_delay > 0:
                    time.sleep(self.publish_delay)
            except OSError:
                if not self.stop.is_set():
                    self.errors += 1
                break

    def close(self) -> None:
        self.stop.set()
        self.client.close()
        if self.thread:
            self.thread.join(timeout=1.0)


def run(cmd: list[str], cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def stop_ports(ports: list[int]) -> None:
    args = ["fuser", "-k"] + [f"{p}/tcp" for p in ports]
    subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.2)


def wait_port(port: int, timeout: float = 5.0) -> None:
    end = time.time() + timeout
    while time.time() < end:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.3):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError(f"port {port} did not open")


def build_field_binary(
    out: Path,
    mqtt_port: int,
    p2p_port: int,
    disc_port: int,
    admission: int,
    topic_max_subs: int,
) -> None:
    srcs = [
        "src/broker.c",
        "src/client.c",
        "src/main.c",
        "src/packet.c",
        "src/session.c",
        "src/topic.c",
        "src/p2p_discover.c",
        "src/p2p_election.c",
        "src/p2p_peer.c",
        "src/p2p_router.c",
        "src/p2p_shard.c",
        "platform/posix/platform_posix.c",
    ]
    cmd = [
        "gcc",
        "-Wall",
        "-Wextra",
        "-std=c11",
        "-g",
        "-D_POSIX_C_SOURCE=200809L",
        "-DCONFIG_MQTT_P2P_DYNAMIC",
        "-DCONFIG_MQTT_P2P_STATIC_SEEDS_ONLY",
        "-DMQTT_MAX_CLIENTS=64",
        f"-DMQTT_ADMISSION_MAX_CLIENTS={admission}",
        f"-DTOPIC_MAX_SUBS={topic_max_subs}" if topic_max_subs > 0 else "-DTOPIC_MAX_SUBS=512",
        f"-DMQTT_BROKER_PORT={mqtt_port}",
        f"-DP2P_PORT={p2p_port}",
        f"-DP2P_DISCOVERY_PORT={disc_port}",
        "-Iinclude",
        "-I.",
        *srcs,
        "-o",
        str(out),
        "-lpthread",
    ]
    run(cmd, cwd=BROKER_DIR)


def start_mosquitto(outdir: Path, ports: list[int]) -> list[subprocess.Popen]:
    procs = []
    for idx, port in enumerate(ports):
        conf = outdir / f"mosquitto_{idx}.conf"
        conf.write_text(
            f"listener {port} 127.0.0.1\n"
            "allow_anonymous true\n"
            "persistence false\n"
            "log_type error\n",
            encoding="utf-8",
        )
        log = open(outdir / f"mosquitto_{idx}.log", "w", encoding="utf-8")
        procs.append(subprocess.Popen(["mosquitto", "-c", str(conf)], stdout=log, stderr=log))
    for port in ports:
        wait_port(port)
    return procs


def start_field(
    outdir: Path,
    ports: list[int],
    p2p_ports: list[int],
    admission: int,
    topic_max_subs: int,
) -> list[subprocess.Popen]:
    disc_port = ports[0] + 4000
    bins = []
    for idx, port in enumerate(ports):
        binary = outdir / f"field_b{idx + 1}"
        build_field_binary(binary, port, p2p_ports[idx], disc_port, admission, topic_max_subs)
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


def connect_with_policy(
    role: str,
    num: int,
    intended: int,
    ports: list[int],
    fallback: bool,
    publish_delay: float = 0.0,
    topics: list[str] | None = None,
    topic_offset: int = 0,
) -> tuple[list[Subscriber | Publisher], int]:
    connected: list[Subscriber | Publisher] = []
    rejected = 0
    for i in range(num):
        order = list(range(len(ports))) if fallback else [intended]
        if fallback:
            order = order[intended:] + order[:intended]
        connected_client = None
        chosen = -1
        chosen_topic = ""
        for broker_idx in order:
            try:
                client = MqttClient(f"{role}{intended}{i}b{broker_idx}", "127.0.0.1", ports[broker_idx])
            except OSError:
                continue
            if role.startswith("sub"):
                assert topics is not None
                topic = topics[(topic_offset + i) % len(topics)]
                try:
                    if not client.subscribe(topic):
                        client.close()
                        continue
                except OSError:
                    client.close()
                    continue
                chosen_topic = topic
            connected_client = client
            chosen = broker_idx
            break
        if connected_client is None:
            rejected += 1
            continue
        if role.startswith("sub"):
            connected.append(Subscriber(connected_client, intended, chosen, chosen_topic))
        else:
            connected.append(Publisher(connected_client, intended, chosen, publish_delay))
    return connected, rejected


def counts_by_broker(clients: list[Subscriber | Publisher], brokers: int) -> list[int]:
    counts = [0] * brokers
    for client in clients:
        counts[client.connected] += 1
    return counts


def fallback_count(clients: list[Subscriber | Publisher]) -> int:
    return sum(1 for client in clients if client.connected != client.intended)


def topic_counts_by_broker(subscribers: list[Subscriber], brokers: int) -> list[int]:
    per_broker: list[set[str]] = [set() for _ in range(brokers)]
    for sub in subscribers:
        per_broker[sub.connected].add(sub.topic)
    return [len(topics) for topics in per_broker]


def expected_deliveries(publishers: list[Publisher], subscribers: list[Subscriber]) -> int:
    subs_by_topic: dict[str, int] = {}
    for sub in subscribers:
        subs_by_topic[sub.topic] = subs_by_topic.get(sub.topic, 0) + 1

    expected = 0
    for pub in publishers:
        for topic, sent in pub.sent_by_topic.items():
            expected += sent * subs_by_topic.get(topic, 0)
    return expected


def run_case(args: argparse.Namespace) -> dict:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    outdir = OUT_ROOT / f"{stamp}-{args.mode}-{args.impl}"
    outdir.mkdir(parents=True, exist_ok=True)

    base = args.port_base
    if args.impl == "mosquitto":
        ports = [base + i for i in range(args.broker_count)]
        p2p_ports = []
    else:
        ports = [base + 100 + i for i in range(args.broker_count)]
        p2p_ports = [base + 300 + i for i in range(args.broker_count)]
    stop_ports(ports + p2p_ports)

    topics = [f"site/lb/{args.mode}/{stamp}/topic-{i}" for i in range(args.topic_count)]
    admission = args.admission
    fallback = args.impl == "field_fallback"
    procs: list[subprocess.Popen] = []
    subscribers: list[Subscriber] = []
    publishers: list[Publisher] = []
    rejected_subs = 0
    rejected_pubs = 0

    try:
        if args.impl == "mosquitto":
            procs = start_mosquitto(outdir, ports)
            time.sleep(args.settle)
        elif args.impl in ("field_no_fallback", "field_fallback"):
            procs = start_field(outdir, ports, p2p_ports, admission, args.topic_max_subs)
            time.sleep(args.settle)
        else:
            raise ValueError(args.impl)

        if args.mode == "hotspot":
            sub_plan = [(0, args.hotspot_subscribers)]
            pub_plan = [(0, args.hotspot_publishers)]
        elif args.mode == "uneven":
            sub_plan = list(enumerate(args.uneven_subscribers))
            pub_plan = list(enumerate(args.uneven_publishers))
        elif args.mode == "fair4":
            sub_plan = [(0, args.fair4_subscribers)]
            pub_plan = [(0, args.fair4_publishers)]
        elif args.mode == "dynamic_burst":
            sub_plan = []
            pub_plan = []
        elif args.mode == "topic_limit":
            sub_plan = []
            pub_plan = []
        else:
            raise ValueError(args.mode)

        if args.mode in ("dynamic_burst", "topic_limit"):
            # Preload a hot broker and three lightly-loaded peers without using
            # fallback, then send a burst of new clients to the hot broker.
            if args.mode == "topic_limit":
                preload_subs = list(enumerate(args.topic_preload_subscribers))
                preload_pubs = list(enumerate(args.topic_preload_publishers))
                burst_subscribers = args.topic_burst_subscribers
            else:
                preload_subs = list(enumerate(args.dynamic_preload_subscribers))
                preload_pubs = list(enumerate(args.dynamic_preload_publishers))
                burst_subscribers = args.dynamic_burst_subscribers
            for intended, count in preload_pubs:
                clients, rejected = connect_with_policy(
                    "pub", count, intended, ports, False, publish_delay=args.publish_delay
                )
                publishers.extend(clients)  # type: ignore[arg-type]
                rejected_pubs += rejected
            for intended, count in preload_subs:
                clients, rejected = connect_with_policy(
                    "sub", count, intended, ports, False, topics=topics, topic_offset=sum(c for _, c in preload_subs[:intended])
                )
                subscribers.extend(clients)  # type: ignore[arg-type]
                rejected_subs += rejected
            clients, rejected = connect_with_policy(
                "subburst", burst_subscribers, 0, ports, fallback,
                topics=topics, topic_offset=sum(count for _, count in preload_subs)
            )
            subscribers.extend(clients)  # type: ignore[arg-type]
            rejected_subs += rejected
            sub_plan = preload_subs + [(0, burst_subscribers)]
            pub_plan = preload_pubs
        else:
            for intended, count in pub_plan:
                clients, rejected = connect_with_policy(
                    "pub", count, intended, ports, fallback, publish_delay=args.publish_delay
                )
                publishers.extend(clients)  # type: ignore[arg-type]
                rejected_pubs += rejected

            for intended, count in sub_plan:
                clients, rejected = connect_with_policy(
                    "sub", count, intended, ports, fallback,
                    topics=topics, topic_offset=sum(c for _, c in sub_plan[:intended])
                )
                subscribers.extend(clients)  # type: ignore[arg-type]
                rejected_subs += rejected

        for sub in subscribers:
            sub.start()
        time.sleep(args.propagate)
        for pub in publishers:
            pub.start(topics)

        time.sleep(args.duration)
    finally:
        for pub in publishers:
            pub.close()
        time.sleep(args.drain)
        for sub in subscribers:
            sub.close()
        for proc in procs:
            proc.terminate()
        for proc in procs:
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=2)

    sent = sum(pub.sent for pub in publishers)
    pub_errors = sum(pub.errors for pub in publishers)
    received = sum(sub.received for sub in subscribers)
    connected_subs = len(subscribers)
    connected_pubs = len(publishers)
    expected_fanout = expected_deliveries(publishers, subscribers)
    delivery_rate = (received * 100.0 / expected_fanout) if expected_fanout else 0.0

    result = {
        "case": {
            "mode": args.mode,
            "impl": args.impl,
            "duration_sec": args.duration,
            "admission": admission if args.impl.startswith("field") else None,
            "topic_count": args.topic_count,
            "topic_max_subs": args.topic_max_subs if args.impl.startswith("field") else None,
            "note": "mosquitto has no broker bridge or fallback; this mode is aggregate local broker traffic"
            if args.impl == "mosquitto" and args.mode in ("uneven", "dynamic_burst")
            else "",
        },
        "requested": {
            "subscribers": sum(c for _, c in sub_plan),
            "publishers": sum(c for _, c in pub_plan),
        },
        "connected": {
            "subscribers": connected_subs,
            "publishers": connected_pubs,
            "clients_by_broker": [
                s + p
                for s, p in zip(
                    counts_by_broker(subscribers, args.broker_count),
                    counts_by_broker(publishers, args.broker_count),
                )
            ],
            "subscribers_by_broker": counts_by_broker(subscribers, args.broker_count),
            "publishers_by_broker": counts_by_broker(publishers, args.broker_count),
            "topics_by_broker": topic_counts_by_broker(subscribers, args.broker_count),
            "topic_subscriptions": connected_subs,
        },
        "rejected": {
            "subscribers": rejected_subs,
            "publishers": rejected_pubs,
        },
        "fallback": {
            "subscriber_fallback_count": fallback_count(subscribers),
            "publisher_fallback_count": fallback_count(publishers),
        },
        "traffic": {
            "published_messages": sent,
            "received_messages": received,
            "throughput_msg_per_sec": round(received / args.duration, 2),
            "delivery_rate_percent": round(delivery_rate, 2),
            "publisher_errors": pub_errors,
        },
        "artifacts": {
            "outdir": str(outdir),
        },
    }
    (outdir / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return result


def parse_csv_ints(value: str) -> list[int]:
    return [int(part.strip()) for part in value.split(",") if part.strip()]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["hotspot", "uneven", "fair4", "dynamic_burst", "topic_limit"], required=True)
    parser.add_argument(
        "--impl",
        choices=["mosquitto", "field_no_fallback", "field_fallback"],
        required=True,
    )
    parser.add_argument("--duration", type=int, default=20)
    parser.add_argument("--settle", type=float, default=5.0)
    parser.add_argument("--propagate", type=float, default=4.0)
    parser.add_argument("--drain", type=float, default=1.0)
    parser.add_argument("--publish-delay", type=float, default=0.0005)
    parser.add_argument("--admission", type=int, default=12)
    parser.add_argument("--broker-count", type=int, default=3)
    parser.add_argument("--topic-count", type=int, default=1)
    parser.add_argument("--topic-max-subs", type=int, default=0)
    parser.add_argument("--port-base", type=int, default=23000)
    parser.add_argument("--hotspot-subscribers", type=int, default=24)
    parser.add_argument("--hotspot-publishers", type=int, default=4)
    parser.add_argument("--fair4-subscribers", type=int, default=4)
    parser.add_argument("--fair4-publishers", type=int, default=4)
    parser.add_argument("--dynamic-preload-subscribers", type=parse_csv_ints, default=parse_csv_ints("7,2,2,2"))
    parser.add_argument("--dynamic-preload-publishers", type=parse_csv_ints, default=parse_csv_ints("1,0,0,0"))
    parser.add_argument("--dynamic-burst-subscribers", type=int, default=18)
    parser.add_argument("--topic-preload-subscribers", type=parse_csv_ints, default=parse_csv_ints("16,4,4,4"))
    parser.add_argument("--topic-preload-publishers", type=parse_csv_ints, default=parse_csv_ints("1,0,0,0"))
    parser.add_argument("--topic-burst-subscribers", type=int, default=36)
    parser.add_argument("--uneven-subscribers", type=parse_csv_ints, default=parse_csv_ints("16,6,2"))
    parser.add_argument("--uneven-publishers", type=parse_csv_ints, default=parse_csv_ints("4,2,1"))
    args = parser.parse_args()

    if (
        args.mode == "uneven"
        and (len(args.uneven_subscribers) != args.broker_count
             or len(args.uneven_publishers) != args.broker_count)
    ):
        raise SystemExit("uneven distributions must match --broker-count")
    if args.mode == "fair4" and args.broker_count != 4:
        raise SystemExit("fair4 requires --broker-count 4")
    if args.mode == "dynamic_burst" and (
        len(args.dynamic_preload_subscribers) != args.broker_count
        or len(args.dynamic_preload_publishers) != args.broker_count
    ):
        raise SystemExit("dynamic preload distributions must match --broker-count")
    if args.mode == "topic_limit" and (
        len(args.topic_preload_subscribers) != args.broker_count
        or len(args.topic_preload_publishers) != args.broker_count
    ):
        raise SystemExit("topic preload distributions must match --broker-count")

    run_case(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
