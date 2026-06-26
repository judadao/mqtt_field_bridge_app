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

## 20260626-210127 random broker drop recovery

- Duration: 20s per case
- Field broker admission limit: 32 clients per broker
- Topic count: 16
- Random drop: 2 non-publisher broker(s), seed `260626`
- Workload: one publisher remains on broker A; subscribers initially target A/B/C/D.
- Artifacts: `/home/judd/moxa/personal/mqtt_field_bridge_app/tests/linux/out/random_drop_recovery/20260626-210127`

| Impl | Dropped brokers | Topic subs | Topics A/B/C/D | Conn clients A/B/C/D | Conn subs A/B/C/D | Rej subs | Fallback subs | Published | Received | Msg/s | Delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| field_no_fallback | 1/2 | 12 | 4/0/0/8 | 5/0/0/8 | 4/0/0/8 | 16 | 0 | 35832 | 26876 | 1343.8 | 100.0 |
| field_fallback | 1/2 | 28 | 4/0/0/16 | 5/0/0/24 | 4/0/0/24 | 0 | 16 | 35812 | 62672 | 3133.6 | 100.0 |

