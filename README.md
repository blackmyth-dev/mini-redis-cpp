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

> Nếu bạn chưa học network, TCP, socket, HTTP, MQTT hoặc SOME/IP, hãy đọc
> **[Hướng dẫn project từ căn bản](docs/PROJECT_GUIDE_VI.md)**. Tài liệu giải thích
> từ mô hình client/server, luồng của một socket, cấu trúc HTTP request cho tới
> cách từng khái niệm ánh xạ vào source code của project.

## Project hiện có gì?

| Thành phần | Trạng thái | Vai trò |
|---|---|---|
| Key-value store, TTL, snapshot | Đã có | Lưu state dùng chung |
| Redis-like protocol trên TCP | Đã có | Giao diện TCP đơn giản để học socket |
| HTTP/1.1 management API | Đã có | Quản lý cùng state bằng `curl` |
| MQTT adapter | Chưa có | Milestone tiếp theo |
| SOME/IP adapter | Chưa có | Milestone sau MQTT |

Hai server hiện tại là hai “cửa vào” khác nhau nhưng cùng đọc/ghi một
`KeyValueStore`. Vì vậy dữ liệu ghi qua TCP có thể đọc qua HTTP và ngược lại.

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

## Tài liệu

- [Hướng dẫn project từ căn bản](docs/PROJECT_GUIDE_VI.md): bắt đầu ở đây nếu
  chưa biết networking.
- [Learning path](docs/LEARNING_PATH.md): các milestone và bài tập để phát triển
  project.

Mục tiêu là hiểu từng tầng, không chỉ ghép thư viện.
