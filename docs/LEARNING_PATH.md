# Lộ trình học từ người mới đến mức phỏng vấn

Không học theo thứ tự tên thư viện. Học từ tầng thấp lên tầng cao và luôn làm
ba việc: dự đoán, quan sát, rồi tự giải thích lại không nhìn tài liệu.

## Cách học một chủ đề

Với mỗi milestone, hãy viết được một trang theo khung:

1. Problem: công nghệ giải quyết vấn đề gì?
2. Mechanism: dữ liệu đi qua các bước nào?
3. Guarantee: nó bảo đảm và không bảo đảm gì?
4. Evidence: dòng code/config nào trong repo thể hiện điều đó?
5. Failure: ba lỗi thường gặp và cách khoanh vùng?

Chỉ coi là hiểu khi vẽ được data flow và trả lời được câu hỏi “tại sao”.

## Milestone 0 — IP, port, socket và framing

Đọc phần TCP trong `PROTOCOLS_VI.md`.

- Phân biệt IP với port, listening socket với connection socket.
- Giải thích TCP là byte stream và không có message boundary.
- Dùng `nc` gửi command bị chia thành nhiều lần gõ.
- Chỉ ra newline framing trong `command_server.cpp`.

Hoàn thành khi trả lời được: “một lần `send` có luôn ứng với một lần `read`
không, và project tìm cuối command bằng cách nào?”

## Milestone 1 — Domain và concurrency

- Đọc `StateStore` và `StateEventBus`.
- Vẽ luồng `set → StateChanged → adapter callbacks`.
- Giải thích vì sao publish event diễn ra sau khi nhả store mutex.
- Viết concurrent test và chạy TSan.

Hoàn thành khi giải thích được domain không biết HTTP/MQTT/SOME/IP nhưng vẫn
phục vụ được tất cả adapter.

## Milestone 2 — Boost.Asio TCP

- Hiểu `io_context`, accept/read/write bất đồng bộ.
- Giải thích lifetime bằng `shared_from_this`.
- Thêm command `TTL` và parser tests.
- Thêm timeout mà không block worker.
- Test hai client đồng thời.

Câu hỏi phỏng vấn:

1. Async I/O khác một-thread-mỗi-connection thế nào?
2. Vì sao buffer và session phải sống tới lúc callback chạy?
3. TCP reliable nhưng application timeout vẫn cần vì sao?

## Milestone 3 — HTTP và Boost.Beast

- Dùng `curl -v` quan sát start-line, header và body.
- Phân biệt TCP framing với HTTP framing.
- Lần theo typed request/response trong `http_server.cpp`.
- Thêm body limit, deadline và JSON error model.
- Thêm TLS bằng `beast::ssl_stream`.

Câu hỏi phỏng vấn:

1. HTTP giải quyết phần nào mà TCP không giải quyết?
2. `PUT` có tính idempotent nghĩa là gì?
3. Vì sao không nên tự parse `Content-Length`?
4. Beast làm gì và business adapter còn phải làm gì?

## Milestone 4 — MQTT và Paho

- Chạy Mosquitto, dùng `mosquitto_pub/sub` quan sát topic.
- Vẽ publisher → broker → subscriber.
- Test reconnect và duplicate QoS 1.
- Thử retained message, Last Will và persistent session.
- Thêm credentials/TLS.
- Unit-test callback mapping bằng fake domain port.

Câu hỏi phỏng vấn:

1. Broker tạo decoupling gì?
2. QoS 0/1/2 khác nhau về delivery contract nào?
3. “Exactly once MQTT” có đồng nghĩa database update đúng một lần không?

## Milestone 5 — SOME/IP trước, vsomeip sau

Đọc phần SOME/IP trong `PROTOCOLS_VI.md`, sau đó đọc
`VSOMEIP_GUIDE_VI.md`.

- Tự vẽ Service/Instance/Method/Event/Eventgroup.
- Phân biệt SOME/IP data plane với SOME/IP-SD discovery.
- Phân biệt standard SOME/IP với implementation vsomeip.
- Lần theo lifecycle `init → start → ST_REGISTERED → offer → stop`.
- Viết client request GET/SET.
- Subscribe eventgroup và nhận field notification.
- Quan sát Client ID/Session ID của request/response.
- Chạy hai network namespace với SD multicast.
- Sau đó học CommonAPI/Franca hoặc AUTOSAR generated bindings.

Câu hỏi phỏng vấn:

1. Routing manager, service application và client application khác nhau gì?
2. Vì sao offer service chưa đủ để client nhận event?
3. vsomeip xử lý envelope hay business payload?
4. Local IPC có thể che lỗi network nào?

## Milestone 6 — Kết nối toàn hệ thống

Thực hiện bốn luồng và giải thích từng bước:

1. TCP SET → HTTP GET.
2. HTTP PUT → MQTT state event.
3. MQTT command → SOME/IP notification.
4. SOME/IP SET → persistence snapshot.

Với mỗi luồng, ghi rõ:

- protocol boundary;
- library callback;
- adapter mapping;
- domain operation;
- thread có thể đang chạy;
- lỗi được trả/ghi log ở đâu.

## Kỹ thuật ghi nhớ

- **Active recall**: đóng tài liệu rồi vẽ lại luồng.
- **Feynman**: giải thích cho người chưa biết trong hai phút, không dùng jargon
  chưa định nghĩa.
- **Contrast**: luôn lập cặp TCP/UDP, HTTP/MQTT, SOME/IP/vsomeip,
  method/event.
- **Break it**: cố ý sai port, ID, byte order, eventgroup và dự đoán triệu
  chứng.
- **Spaced repetition**: ôn lại sau 1, 3, 7 và 14 ngày.

Một flashcard tốt hỏi về cơ chế: “Tại sao TCP không giữ message boundary?”.
Flashcard kém chỉ hỏi viết tắt: “TCP là gì?”.

## Definition of done

- Core tests pass normal, ASan và TSan.
- Full build tìm đúng Boost/Paho/vsomeip, không có stub.
- Có thể vẽ toàn bộ gateway từ network tới domain mà không nhìn tài liệu.
- Ghi state qua một adapter và đọc/nhận event qua adapter khác.
- Debug có thứ tự theo tầng, không sửa config ngẫu nhiên.
- Ctrl+C dừng Paho, vsomeip, Asio và snapshot theo đúng thứ tự.
- Trả lời mỗi câu phỏng vấn bằng problem, mechanism, trade-off và project
  evidence.
