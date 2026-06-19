# Versioning

Use separate tag namespaces for the broker module and the product bridge app.

## Broker Module Tags

Broker module tags belong to the `mqtt_min_broker` repository.

Preferred format:

```text
minmqtt-vX.Y.Z
```

Example:

```text
minmqtt-v0.1.0
```

The product app references this tag in `deps.json`:

```json
"version": "minmqtt-v0.1.0"
```

## Product Bridge Tags

Product tags belong to this repository.

Format:

```text
bridge-vX.Y.Z
```

Example:

```text
bridge-v0.1.0
```

Use product tags for field app releases, not for broker module releases.

## Branch Names

Recommended product branches:

```text
main
product/mqtt-bridge
feature/<short-topic>
fix/<short-topic>
```

The initial product work should happen on:

```text
product/mqtt-bridge
```
