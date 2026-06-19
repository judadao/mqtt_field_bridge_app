# Field Bridge Scenario

The product app targets configurable field broker meshes. The first validation
setup uses three notebooks, but the peer table defaults to 10 slots and can be
raised at build time for larger deployments:

- A source node has an ESP32 broker and receives or owns field data.
- Peer nodes have ESP32 brokers and subscribe to topics through the mesh.
- Larger lab setups can use 10 brokers or more when memory and network capacity
  are sized for the target.
- The bridge graph only needs to be connected. For example, node 1 bridged to
  node 2 and node 3 bridged to node 2 should behave as one broker network.

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

Suggested peer subscription:

```text
site/field-a/data/#
```
