# bridge-v0.1.12 Release Notes

Release date: 2026-07-01

Product tag: `bridge-v0.1.12`

Broker dependency: `mqtt_min_broker` pinned to `mqtt-v0.1.27`

## Summary

This release completes the product-side adoption of MQTT QoS 0/1/2 fallback
support. It pins the broker module release that adds fallback ingress QoS
handshakes and P2P QoS publish buffering, then updates product validation so the
fallback client path is tested with QoS 2.

## Changes

- Pinned `deps.json` to `mqtt_min_broker` `mqtt-v0.1.27`.
- Allowed product publish-test records to use MQTT QoS 0, 1, or 2.
- Updated provisioning HTTP publish-test parsing to preserve QoS 2 instead of
  treating `qos` as a boolean.
- Raised the product Linux fallback client test's key primary/fallback/mesh
  paths to QoS 2.
- Documented current recovery behavior and QoS coverage in `README.md`.

## Broker Capabilities Adopted

- Primary MQTT broker path supports QoS 0/1/2.
- Mesh-only fallback ingress supports QoS 0/1/2 publish and subscribe
  handshakes.
- QoS 1 fallback ingress uses PUBACK.
- QoS 2 fallback ingress uses PUBREC, PUBREL, and PUBCOMP.
- Fallback delivery to subscribers tracks QoS 1/2 inflight packets and retries
  pending sends.
- P2P mesh routing uses TCP transport, `(origin, seq)` duplicate suppression,
  and a fixed in-memory pending queue for routed QoS 1/2 publishes that
  temporarily cannot reach a known next hop.

## Validation

Product validation run for this release:

```text
make -C tests/linux unit-tests
make -C tests/linux client-fallback-test
```

Broker module validation run before pinning `mqtt-v0.1.27`:

```text
make -f Makefile.linux unit-tests
./scripts/test_qos2.sh
./scripts/test_session_qos2.sh
./scripts/test_p2p_dynamic.sh
./scripts/test_fallback_ingress_qos.sh
```

## Notes

The fallback ingress is still a mesh ingress path, not a full replacement
broker. QoS 1/2 MQTT handshakes are completed on the fallback connection, but
the P2P pending queue is in-memory and bounded. QoS 0 messages already in flight
at the primary-listener failure boundary can still be lost.
