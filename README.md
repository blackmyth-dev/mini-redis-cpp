# C++ Edge Gateway

Một project học C++ systems programming theo bốn chặng nhưng cùng một sản phẩm:

```text
Device/SOME-IP <---> Edge Gateway <---> MQTT broker
                         |
                         +-- HTTP management API
                         +-- Redis-like state store
```

Phiên bản hiện tại có Redis-like TCP server và HTTP management API dùng chung
thread-safe store, cùng thread pool, TTL, snapshot và graceful shutdown.

## Build và chạy

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/edge_gateway --port 6379 --http-port 8080 --db gateway.db --threads 4
```

Kết nối bằng `nc localhost 6379`, mỗi command kết thúc bằng newline:

```text
SET name Duy
GET name
EXPIRE name 10
TTL name
SAVE
QUIT
```

Response protocol: `+OK`, `$<length>\r\n<value>`, `:<number>`, `$-1`, hoặc
`-ERR <message>`.

HTTP API:

```bash
curl -X PUT --data 'Duy' http://localhost:8080/kv/name
curl http://localhost:8080/kv/name
curl -X DELETE http://localhost:8080/kv/name
curl http://localhost:8080/health
```

Xem [docs/LEARNING_PATH.md](docs/LEARNING_PATH.md) trước khi thêm HTTP, MQTT hay
SOME/IP. Mục tiêu là hiểu từng tầng, không chỉ ghép thư viện.
