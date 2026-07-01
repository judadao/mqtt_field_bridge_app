# Load Balance Throughput Results

## 20260626-194540 load-balance throughput matrix

- Duration: 20s per case
- Field broker admission limit: 12 clients per broker
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/load_balance_matrix/20260626-194540`
- Note: mosquitto has no bridge or fallback; uneven mosquitto is aggregate local broker traffic.

| Mode | Impl | Conn subs A/B/C | Conn pubs A/B/C | Rej subs | Fallback subs | Published | Received | Msg/s | Delivery % |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| hotspot | mosquitto | 24/0/0 | 4/0/0 | 0 | 0 | 112483 | 1491995 | 74599.75 | 55.27 |
| hotspot | field_no_fallback | 8/0/0 | 4/0/0 | 16 | 0 | 139001 | 1112008 | 55600.4 | 100.0 |
| hotspot | field_fallback | 8/12/4 | 4/0/0 | 0 | 16 | 134829 | 438444 | 21922.2 | 13.55 |
| uneven | mosquitto | 16/6/2 | 4/2/1 | 0 | 0 | 195997 | 1393344 | 69667.2 | 29.62 |
| uneven | field_no_fallback | 8/6/2 | 4/2/1 | 8 | 0 | 243262 | 258292 | 12914.6 | 6.64 |
| uneven | field_fallback | 8/10/6 | 4/2/1 | 0 | 12 | 237296 | 376434 | 18821.7 | 6.61 |

## 20260626-195834 fair4 admission=2 throughput

- Duration: 20s per case
- Field broker admission limit: 2 clients per broker
- Workload: 4 publishers and 4 subscribers all initially target broker A.
- Expected fallback layout: 4 brokers x 2 clients = `2/2/2/2` total capacity.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/fair4_admission2/20260626-195834`

| Impl | Conn clients A/B/C/D | Conn subs A/B/C/D | Conn pubs A/B/C/D | Rej subs | Rej pubs | Fallback subs | Fallback pubs | Published | Received | Msg/s | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mosquitto | 8/0/0/0 | 4/0/0/0 | 4/0/0/0 | 0 | 0 | 0 | 0 | 143049 | 572196 | 28609.8 | 100.0 |
| field_no_fallback | 2/0/0/0 | 0/0/0/0 | 2/0/0/0 | 4 | 2 | 0 | 0 | 71632 | 0 | 0.0 | 0.0 |
| field_fallback | 2/2/2/2 | 0/0/2/2 | 2/2/0/0 | 0 | 0 | 4 | 2 | 143034 | 572136 | 28606.8 | 100.0 |

## 20260626-200411 fair4 capacity comparison

- Duration: 20s per case
- No-fallback field admission: 8 on broker A, expected `8/0/0/0`
- Fallback field admission: 2 per broker, expected `2/2/2/2`
- Workload: 4 publishers and 4 subscribers all initially target broker A.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/fair4_capacity_compare/20260626-200411`

| Impl | Admission | Conn clients A/B/C/D | Conn subs A/B/C/D | Conn pubs A/B/C/D | Rej subs | Rej pubs | Fallback subs | Fallback pubs | Published | Received | Msg/s | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mosquitto | - | 8/0/0/0 | 4/0/0/0 | 4/0/0/0 | 0 | 0 | 0 | 0 | 143091 | 572364 | 28618.2 | 100.0 |
| field_no_fallback | 8 | 8/0/0/0 | 4/0/0/0 | 4/0/0/0 | 0 | 0 | 0 | 0 | 142995 | 571980 | 28599.0 | 100.0 |
| field_fallback | 2 | 2/2/2/2 | 0/0/2/2 | 2/2/0/0 | 0 | 0 | 4 | 2 | 143151 | 329252 | 16462.6 | 57.5 |

## 20260626-201422 dynamic balance burst

- Duration: 20s per case
- Field broker admission limit: 8 clients per broker
- Preload: broker A has 7 subscribers + 1 publisher; B/C/D have 2 subscribers each.
- Burst: 18 new subscribers all try broker A first.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/dynamic_balance_burst/20260626-201422`

| Impl | Conn clients A/B/C/D | Conn subs A/B/C/D | Rej subs | Fallback subs | Published | Received | Msg/s | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| field_no_fallback | 8/2/2/2 | 7/2/2/2 | 18 | 0 | 1 | 0 | 0.0 | 0.0 |
| field_fallback | 8/8/8/8 | 7/8/8/8 | 0 | 18 | 33416 | 473999 | 23699.95 | 45.76 |

## 20260626-2017-r2 dynamic balance burst

- Duration: 20s per case
- Field broker admission limit: 8 clients per broker
- Preload: broker A has 7 subscribers + 1 publisher; B/C/D have 2 subscribers each.
- Burst: 18 new subscribers all try broker A first.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/dynamic_balance_burst/20260626-2017-r2`

| Impl | Conn clients A/B/C/D | Conn subs A/B/C/D | Rej subs | Fallback subs | Published | Received | Msg/s | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| field_no_fallback | 8/2/2/2 | 7/2/2/2 | 18 | 0 | 35311 | 163861 | 8193.05 | 35.7 |
| field_fallback | 8/8/8/8 | 7/8/8/8 | 0 | 18 | 33074 | 536816 | 26840.8 | 52.36 |

## 20260626-topic32 dynamic balance burst

- Duration: 20s per case
- Field broker admission limit: 8 clients per broker
- Topic count: 32
- Preload: broker A has 7 subscribers + 1 publisher; B/C/D have 2 subscribers each.
- Burst: 18 new subscribers all try broker A first.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/dynamic_balance_burst/20260626-topic32`

Column meanings:
- `Topics`: configured test topic count.
- `Topic subs`: accepted subscriber-topic registrations.
- `Topics A/B/C/D`: distinct subscribed topics present on each broker.
- `Conn clients A/B/C/D`: total connected MQTT clients on each broker.
- `Conn subs A/B/C/D`: connected subscribers on each broker.
- `Rej subs`: burst subscribers that could not connect.
- `Fallback subs`: burst subscribers accepted by a fallback broker.

| Impl | Topics | Topic subs | Topics A/B/C/D | Conn clients A/B/C/D | Conn subs A/B/C/D | Rej subs | Fallback subs | Published | Received | Msg/s | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| field_no_fallback | 32 | 13 | 7/2/2/2 | 8/2/2/2 | 7/2/2/2 | 18 | 0 | 35810 | 14549 | 727.45 | 100.0 |
| field_fallback | 32 | 31 | 7/8/8/8 | 8/8/8/8 | 7/8/8/8 | 0 | 18 | 35800 | 34682 | 1734.1 | 100.0 |

## 20260626-topic-limit topic-limit burst

- Duration: 20s per case
- Client admission limit: 64 clients per broker
- Topic subscription table limit: 16 entries per broker
- Test topic count: 64
- Preload: broker A has 16 topic subscriptions; B/C/D have 4 each.
- Burst: 36 new topic subscribers all try broker A first.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/topic_limit_burst/20260626-topic-limit`

Column meanings:
- `Topic subs`: accepted subscriber-topic registrations across all brokers.
- `Topics A/B/C/D`: accepted topic subscriptions on each broker.
- `Clients A/B/C/D`: total connected MQTT clients on each broker.
- `Rej subs`: burst subscribers rejected because no topic slot was available.
- `Fallback subs`: burst subscribers accepted by another broker after A was full.

| Impl | Topic subs | Topics A/B/C/D | Clients A/B/C/D | Rej subs | Fallback subs | Published | Received | Msg/s | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| field_no_fallback | 28 | 16/4/4/4 | 17/4/4/4 | 36 | 0 | 35832 | 10976 | 548.8 | 70.0 |
| field_fallback | 64 | 16/16/16/16 | 17/16/16/16 | 0 | 36 | 35805 | 35805 | 1790.25 | 100.0 |

## 20260626-214321 random broker drop recovery

- Duration: 20s per case
- Field broker admission limit: 8 clients per broker
- Topic count: 16
- Random drop: 2 broker(s), seed `260626`
- Workload: publishers `1,1,1,1` and subscribers `4,4,4,4` initially target A/B/C/D.
- Topic model: each intended broker has local topics; full-workload delivery compares expected total deliveries with actual received deliveries.
- Note: mosquitto has no broker bridge or fallback; it is the independent-broker baseline.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/random_drop_recovery/20260626-214321`

| Impl | Dropped brokers | Req clients A/B/C/D | Conn clients A/B/C/D | Conn subs A/B/C/D | Conn pubs A/B/C/D | Rej subs | Rej pubs | Fallback subs | Fallback pubs | Published | Received | Msg/s | Connected delivery % | Requested delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mosquitto | 0/1 | 5/5/5/5 | 0/0/5/5 | 0/0/4/4 | 0/0/1/1 | 8 | 2 | 0 | 0 | 71615 | 71615 | 3580.75 | 100.0 | 50.0 |
| field_no_fallback | 0/1 | 5/5/5/5 | 0/0/5/5 | 0/0/4/4 | 0/0/1/1 | 8 | 2 | 0 | 0 | 71648 | 71648 | 3582.4 | 100.0 | 50.0 |
| field_fallback | 0/1 | 5/5/5/5 | 0/0/8/8 | 0/0/5/7 | 0/0/3/1 | 4 | 0 | 12 | 2 | 143140 | 107369 | 5368.45 | 100.0 | 75.01 |

## 20260626-fixed-random-failure fixed-message broker failure recovery

- Workload: 4 brokers, 10000 messages per broker, expected total 40000.
- Failure: broker(s) A/B/D are terminated 0.5s after publishing starts, held down for 3.0s, then restarted.
- Metric: received unique payloads versus the fixed expected message count; dropped workload isolates the failed broker workload.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/fixed_failure_recovery/20260626-fixed-random-failure`

| Impl | Dropped | Expected A/B/C/D | Sent A/B/C/D | Received A/B/C/D | Dropped workload | Dropped delivery % | Pub done A/B/C/D | Pub reconnects | Sub reconnects | Missing | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mosquitto | A/B/D | 10000/10000/10000/10000 | 1932/1932/10000/1938 | 1931/1931/10000/1937 | 5799/30000 | 19.33 | 0/0/1/0 | 0/0/0/0 | 0/0/0/0 | 24201 | 39.4975 |
| field_no_fallback | A/B/D | 10000/10000/10000/10000 | 1924/1929/10000/1934 | 1923/1928/10000/1933 | 5784/30000 | 19.28 | 0/0/1/0 | 0/0/0/0 | 0/0/0/0 | 24216 | 39.46 |
| field_fallback | A/B/D | 10000/10000/10000/10000 | 10000/10000/10000/10000 | 9998/9999/10000/9999 | 29996/30000 | 99.9867 | 1/1/1/1 | 2/1/0/1 | 2/1/0/1 | 4 | 99.99 |

Interpretation: the dropped-workload columns isolate only the failed A/B/D
brokers, so healthy broker C does not hide recovery behavior. Mosquitto and field
no-fallback stop the failed-broker publishers around 1900 messages each, leaving
only about 19% of the failed workload delivered. Field fallback reconnects those
failed-broker publishers and subscribers through live broker C, completes every
publisher target, and loses only four QoS 0 messages at the socket-break boundary.

## 20260701-3node-fixed-a-failure fixed-message broker failure recovery

- Workload: 3 brokers, 10000 messages per broker, expected total 30000.
- Failure: broker(s) A are terminated 0.5s after publishing starts, held down for 3.0s, then restarted.
- Metric: received unique payloads versus the fixed expected message count; dropped workload isolates the failed broker workload.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/fixed_failure_recovery/20260701-3node-fixed-a-failure`

| Impl | Dropped | Expected A/B/C | Sent A/B/C | Received A/B/C | Dropped workload | Dropped delivery % | Pub done A/B/C | Pub reconnects | Sub reconnects | Missing | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mosquitto | A | 10000/10000/10000 | 1936/10000/10000 | 1935/10000/10000 | 1935/10000 | 19.35 | 0/1/1 | 0/0/0 | 0/0/0 | 8065 | 73.1167 |
| field_no_fallback | A | 10000/10000/10000 | 1928/10000/10000 | 1927/10000/10000 | 1927/10000 | 19.27 | 0/1/1 | 0/0/0 | 0/0/0 | 8073 | 73.09 |
| field_fallback | A | 10000/10000/10000 | 10000/10000/10000 | 9999/10000/10000 | 9999/10000 | 99.99 | 1/1/1 | 1/0/0 | 1/0/0 | 1 | 99.9967 |

## 20260701-3node-3min-meshfallback-random1-700k fixed-message broker failure recovery

- Workload: 3 brokers, 700000 messages per broker, expected total 2100000.
- Failure: broker A's primary MQTT listener is stopped 60.0s after publishing starts, held down for 10.0s, then restarted; fallback ingress and P2P remain running.
- Metric: received unique payloads versus the fixed expected message count; dropped workload isolates the failed broker workload.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/fixed_failure_recovery/20260701-3node-3min-meshfallback-random1-700k`

| Impl | Dropped | Elapsed sec | Expected A/B/C | Sent A/B/C | Received A/B/C | Dropped workload | Dropped delivery % | Pub done A/B/C | Pub reconnects | Sub reconnects | Pub fallback | Sub fallback | Missing | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mosquitto | A | 182.455 | 700000/700000/700000 | 231024/700000/700000 | 231023/368009/367995 | 231023/700000 | 33.0033 | 0/1/1 | 0/0/0 | 0/0/0 | 0/0/0 | 0/0/0 | 1132973 | 46.0489 |
| field_no_fallback | A | 182.781 | 700000/700000/700000 | 230836/700000/700000 | 230836/354003/353956 | 230836/700000 | 32.9766 | 0/1/1 | 0/0/0 | 0/0/0 | 0/0/0 | 0/0/0 | 1161205 | 44.7045 |
| field_fallback | A | 184.411 | 700000/700000/700000 | 700000/700000/700000 | 698837/699999/699999 | 698837/700000 | 99.8339 | 1/1/1 | 1/0/0 | 1/0/0 | 1/0/0 | 1/0/0 | 1165 | 99.9445 |

## 20260701-3node-3min-meshfallback-random2-700k fixed-message broker failure recovery

- Workload: 3 brokers, 700000 messages per broker, expected total 2100000.
- Failure: broker A/C primary MQTT listeners are stopped 60.0s after publishing starts, held down for 10.0s, then restarted; fallback ingress and P2P remain running.
- Metric: received unique payloads versus the fixed expected message count; dropped workload isolates the failed broker workload.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/fixed_failure_recovery/20260701-3node-3min-meshfallback-random2-700k`

| Impl | Dropped | Elapsed sec | Expected A/B/C | Sent A/B/C | Received A/B/C | Dropped workload | Dropped delivery % | Pub done A/B/C | Pub reconnects | Sub reconnects | Pub fallback | Sub fallback | Missing | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mosquitto | A/C | 182.392 | 700000/700000/700000 | 230181/700000/230186 | 230180/368529/230185 | 460365/1400000 | 32.8832 | 0/1/0 | 0/0/0 | 0/0/0 | 0/0/0 | 0/0/0 | 1271106 | 39.4711 |
| field_no_fallback | A/C | 181.781 | 700000/700000/700000 | 230530/700000/230454 | 230530/354425/230454 | 460984/1400000 | 32.9274 | 0/1/0 | 0/0/0 | 0/0/0 | 0/0/0 | 0/0/0 | 1284591 | 38.829 |
| field_fallback | A/C | 185.194 | 700000/700000/700000 | 700000/700000/700000 | 698841/699999/698844 | 1397685/1400000 | 99.8346 | 1/1/1 | 1/0/1 | 1/0/1 | 1/0/1 | 1/0/1 | 2316 | 99.8897 |
