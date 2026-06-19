# Field Bridge Scenario

The product app targets a three-notebook field setup:

- Note 1 has an ESP32 broker and receives or owns field data.
- Note 2 has an ESP32 broker and subscribes to topics through the mesh.
- Note 3 has an ESP32 broker and behaves like Note 2.

The product app owns:

- WiFi provisioning
- local HTML setup and status UI
- bridge peer configuration
- broker startup policy
- P2P startup policy
- product diagnostics

The broker implementation comes from `deps/mqtt_min_broker` and is pinned by
`deps.json`.

Suggested topic prefix:

```text
site/<site_id>/<stream>
```

Suggested subscription from Note 2 or Note 3:

```text
site/field-a/data/#
```
