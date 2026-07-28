# Learning path

## Milestone 1 — Domain và concurrency

- Đọc `StateStore` và `StateEventBus`.
- Giải thích vì sao publish event diễn ra sau khi nhả store mutex.
- Viết concurrent test và chạy TSan.

## Milestone 2 — Boost.Asio TCP

- Hiểu `io_context`, async accept/read/write và object lifetime bằng
  `shared_from_this`.
- Thêm command `TTL` và parser tests.
- Thêm timeout mà không block worker.

## Milestone 3 — Boost.Beast HTTP

- Lần theo typed request/response.
- Thêm body limit, deadline và JSON error model.
- Thêm TLS bằng `beast::ssl_stream`.

## Milestone 4 — Paho MQTT bridge

- Chạy Mosquitto và quan sát subscribe/publish.
- Test reconnect và duplicate QoS 1.
- Thêm Last Will, credentials và TLS.
- Viết fake domain port để unit-test callback mapping.

## Milestone 5 — COVESA vsomeip

- Chạy service với config local.
- Viết vsomeip client request GET/SET.
- Subscribe eventgroup và nhận field notification.
- Chạy hai network namespace với SD multicast.
- Sau đó học CommonAPI/Franca hoặc AUTOSAR generated bindings.

## Definition of done

- Core tests pass normal, ASan và TSan.
- Full build tìm đúng Boost/Paho/vsomeip, không có stub.
- Ghi state qua một adapter và đọc/nhận event qua adapter khác.
- Ctrl+C dừng Paho, vsomeip, Asio và snapshot theo đúng thứ tự.

