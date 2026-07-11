# Learning path

## Kiến trúc đích

Một `GatewayRuntime` sẽ sở hữu thread pool, state store, event bus và lifecycle.
Các adapter giao tiếp với runtime thay vì gọi chéo trực tiếp:

```text
Redis TCP --+
HTTP -------+--> GatewayRuntime --> KeyValueStore / EventBus
MQTT -------+
SOME/IP ----+
```

Protocol adapter chỉ parse/serialize; business state nằm ở core. Nhờ vậy parser
được test không cần socket và core không phụ thuộc giao thức mạng.

## Milestone 1 — Redis-like state server (hiện tại)

- Hiểu mutex đang bảo vệ invariant nào trong `KeyValueStore`.
- Hiểu vì sao destructor của `ThreadPool` phải join worker.
- Dùng `nc` gửi hai command cùng packet và một command bị chia nhỏ.
- Viết thêm `INCR` theo test-first.
- Chạy AddressSanitizer và ThreadSanitizer.

Done khi 100 client đồng thời, shutdown không treo, snapshot load đúng và không
có data race.

## Milestone 2 — HTTP/1.1 management server (hiện tại)

- Incremental parser: request line, headers, `Content-Length`.
- `GET/PUT/DELETE /kv/{key}` và `/health` đã có; `/metrics` là bài tập tiếp theo.
- Incremental parsing, keep-alive và giới hạn body/header đã có.
- Timeout và static files là bước hardening tiếp theo.
- Không giả định một `recv()` tương ứng một request.

## Milestone 3 — MQTT 3.1.1 subset

- Remaining Length variable-byte encoding.
- CONNECT, PUBLISH QoS 0, SUBSCRIBE và PING.
- Topic filter `+`/`#`, retained message, keep-alive; sau đó mới QoS 1.
- Publish state changes vào `gateway/kv/<key>`.

## Milestone 4 — SOME/IP adapter

- Header serialization theo network byte order.
- Request/response, fire-and-forget, event notification.
- Service discovery state machine; tách UDP/TCP khỏi serializer.
- Sau khi hiểu wire format mới tích hợp stack automotive thực tế.

## Milestone 5 — hardening

- `epoll` reactor, bounded queue/backpressure, config và structured logging.
- Atomic snapshot, append-only log và corruption detection.
- Parser fuzzing, benchmark p50/p95/p99, CI và sanitizers.
