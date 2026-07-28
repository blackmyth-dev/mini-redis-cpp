# C++ Edge Gateway

Project học Modern C++ và middleware automotive theo kiến trúc ports/adapters.
Runtime không tự viết lại protocol stack:

- Boost.Asio cho TCP command transport.
- Boost.Beast cho HTTP/1.1.
- Eclipse Paho MQTT C++ cho MQTT client.
- COVESA vsomeip cho SOME/IP, routing và Service Discovery.

```text
TCP CLI ─┐
HTTP ────┼──> StateStore ──> StateEventBus ──> MQTT state topics
MQTT ────┤                         └──────────> vsomeip field event
vsomeip ─┘
```

## Build core không cần middleware

```bash
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev
```

## Dependency cho full runtime

Ubuntu/Debian:

```bash
sudo apt install libboost-all-dev libpaho-mqtt-dev libpaho-mqttpp-dev
```

Cài COVESA vsomeip theo hướng dẫn chính thức, sao cho
`find_package(vsomeip3 3.4.10)` tìm thấy package:

```bash
git clone https://github.com/COVESA/vsomeip.git
cmake -S vsomeip -B vsomeip/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SIGNAL_HANDLING=1 \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build vsomeip/build --target install
```

Sau đó:

```bash
cmake --preset full
cmake --build --preset full
```

## Chạy

Terminal 1, chạy MQTT broker ngoài:

```bash
mosquitto -v
```

Terminal 2:

```bash
export VSOMEIP_APPLICATION_NAME=edge-gateway
export VSOMEIP_CONFIGURATION="$PWD/config/vsomeip-edge-gateway.json"

./build/full/edge_gateway \
  --tcp-port 6379 \
  --http-port 8080 \
  --mqtt-uri tcp://127.0.0.1:1883 \
  --vsomeip-app edge-gateway \
  --threads 4 \
  --db gateway.db
```

MQTT command topic là `gateway/command/<key>`. State change được publish tới
`gateway/state/<key>`.

## Tài liệu

- [Kiến trúc và UML](docs/ARCHITECTURE_VI.md)
- [HTTP, MQTT và SOME/IP hoạt động thế nào](docs/PROTOCOLS_VI.md)
- [COVESA vsomeip setup và mapping](docs/VSOMEIP_GUIDE_VI.md)
- [Lộ trình học và bài tập](docs/LEARNING_PATH.md)

