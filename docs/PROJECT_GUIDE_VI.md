# Hướng dẫn C++ Edge Gateway từ căn bản

Tài liệu này dành cho người đã biết C++ cơ bản nhưng chưa quen với network,
TCP, socket, HTTP server, MQTT và SOME/IP. Bạn không cần hiểu tất cả ngay lần
đọc đầu. Nên vừa đọc vừa mở các file source được nhắc tới và chạy từng ví dụ.

## 1. Project này giải quyết bài toán gì?

Hãy tưởng tượng một máy tính nhỏ đặt cạnh thiết bị hoặc trong xe. Nó cần nhận dữ
liệu từ thiết bị, giữ trạng thái gần nhất, cho công cụ quản trị đọc trạng thái,
và chuyển dữ liệu tới các hệ thống khác. Máy tính trung gian đó là **edge
gateway**.

```text
Thiết bị/ECU                                      Cloud hoặc ứng dụng
    |                                                       |
    | SOME/IP                              MQTT             |
    +---------->  C++ Edge Gateway  <-----------------------+
                         |
                         +-- HTTP API cho quản trị
                         +-- Redis-like TCP API để học socket
                         +-- KeyValueStore giữ state dùng chung
```

Ở phiên bản hiện tại, Redis-like TCP, HTTP, store, TTL và snapshot đã được viết.
MQTT và SOME/IP **chưa được hiện thực**; chúng là các milestone tiếp theo.

## 2. Bản đồ source code

```text
apps/edge_gateway_main.cpp             Tạo và chạy các component
include/gateway/core/
  key_value_store.hpp                  API kho key-value
  thread_pool.hpp                      Nhóm worker thread
include/gateway/redis/ và src/redis/
  command_parser.cpp                   Text thành Command
  tcp_server.cpp                       socket/bind/listen/accept/recv/send
include/gateway/http/ và src/http/
  http_parser.cpp                      Bytes thành HTTP Request
  http_server.cpp                      TCP server, route và HTTP Response
include/gateway/storage/ và src/storage/
  snapshot_store.cpp                   Lưu/load state vào gateway.db
tests/gateway_tests.cpp                Test store, parser và snapshot
```

Quy tắc thiết kế quan trọng là **tách protocol khỏi business state**:

```text
bytes từ mạng -> parser -> request/command -> KeyValueStore -> response -> bytes
```

Parser không cần socket để test. `KeyValueStore` cũng không cần biết request đến
từ HTTP, TCP, MQTT hay SOME/IP.

## 3. Network, IP và port

Network cho phép hai process trao đổi dữ liệu, kể cả khi chúng chạy trên hai máy
khác nhau. Để tìm đúng process, ta thường cần:

- **IP address**: xác định máy, ví dụ `127.0.0.1`;
- **port**: xác định service trên máy;
- **transport protocol**: project đang dùng TCP.

`127.0.0.1:8080` nghĩa là port `8080` trên chính máy hiện tại. `localhost`
thường được phân giải thành địa chỉ loopback này. Nhiều ứng dụng dùng chung một
IP nhưng mỗi server phải bind vào một port phù hợp. Project dùng:

- port `6379` cho Redis-like TCP server;
- port `8080` cho HTTP server.

### Client và server

- **Server** mở một port và chờ kết nối.
- **Client** chủ động kết nối tới IP và port đó.
- Sau khi kết nối, hai phía gửi và nhận bytes.

Trong project, `edge_gateway` là server; `nc` và `curl` là client.

## 4. TCP là gì?

TCP cung cấp một **luồng byte có thứ tự và đáng tin cậy** giữa hai đầu kết nối.
Nó đảm bảo bytes đến đúng thứ tự, truyền lại dữ liệu thất lạc và tách riêng từng
connection.

TCP **không hiểu message của ứng dụng**. Nếu client gửi hai command, server
không được giả định mỗi lần `recv()` sẽ nhận đúng một command. Thực tế có thể là:

```text
Client gửi: "GET name" + newline, rồi "GET age" + newline
recv #1:    "GET na"
recv #2:    "me" + newline + "GET age" + newline
```

Hoặc một `recv()` nhận cả hai command. Đây là ý quan trọng nhất: **TCP có biên
của byte stream, không có biên message**.

Ứng dụng phải tự định nghĩa cách chia message, gọi là **framing**:

- Redis-like protocol dùng newline kết thúc command;
- HTTP dùng một dòng trống kết thúc headers và `Content-Length` xác định body;
- MQTT dùng trường Remaining Length trong binary header;
- SOME/IP có trường Length trong header.

## 5. Socket là gì?

Socket là API do hệ điều hành cung cấp để process giao tiếp qua network. Trên
Linux/POSIX, `socket()` trả về số nguyên gọi là **file descriptor** (`fd`).

### Vòng đời socket phía server

```text
socket() -> bind(IP, port) -> listen() -> accept()
                                            |
                                            v
                                     client socket mới
                                            |
                                  recv()/send() nhiều lần
                                            |
                                          close()
```

Ý nghĩa từng bước:

1. `socket(AF_INET, SOCK_STREAM, 0)` tạo IPv4 TCP socket.
2. `bind()` gắn socket với local IP và port.
3. `listen()` biến nó thành listening socket.
4. `accept()` chờ kết nối và trả về **client socket khác**.
5. `recv()` đọc bytes từ client socket.
6. `send()` ghi bytes vào client socket.
7. `shutdown()` đánh thức operation đang block và đóng hướng truyền.
8. `close()` giải phóng file descriptor.

Listening socket chỉ nhận kết nối mới. Mỗi connection có một client socket
riêng, nên server có thể nói chuyện độc lập với nhiều client.

### Ánh xạ vào source code

Mở `src/redis/tcp_server.cpp`, hàm `TcpServer::run()`:

```cpp
const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
::bind(server_fd, ...);
::listen(server_fd, SOMAXCONN);
const int client_fd = ::accept(server_fd, nullptr, nullptr);
```

Các tên thường gặp:

- `AF_INET`: IPv4;
- `SOCK_STREAM`: TCP byte stream;
- `INADDR_ANY`: lắng nghe trên mọi IPv4 interface;
- `htons(port_)`: đổi port sang network byte order;
- `SO_REUSEADDR`: giúp bind lại port sớm sau khi restart;
- `SOMAXCONN`: kích thước backlog do hệ điều hành giới hạn.

`0.0.0.0:8080` là địa chỉ bind, không phải địa chỉ client thường nhập. Khi chạy
local, client dùng `localhost:8080`. `accept()` và `recv()` đang là các lời gọi
**blocking**: thread chờ tới khi có kết nối, dữ liệu hoặc socket bị shutdown.

Tương tự `recv()`, một lần `send()` cũng không được đảm bảo ghi hết toàn bộ
buffer. Vì vậy cả hai server có hàm `send_all()` lặp cho tới khi đã gửi đủ số
byte hoặc gặp lỗi. Tên hàm mô tả chính xác lớp bảo đảm mà ứng dụng bổ sung trên
API socket mức thấp.

## 6. Redis-like TCP server hoạt động thế nào?

Đây không phải Redis hoàn chỉnh. Nó là text protocol nhỏ để học socket và state
server.

```text
nc/client -> accept -> ThreadPool -> handle_client(client_fd)
                                        |
                                      recv bytes
                                        |
                                  ghép vào pending
                                        |
                                  tìm ký tự newline
                                        |
                                   CommandParser
                                        |
                                   KeyValueStore
                                        |
                              response -> send_all
```

Trong `handle_client`, buffer cố định là vùng nhận tạm; mọi bytes được nối vào
`pending`. Server chỉ parse khi tìm thấy newline. Nhờ vậy command bị chia qua
nhiều `recv()` vẫn đúng và nhiều command trong một `recv()` vẫn được xử lý hết.
Giới hạn 64 KiB ngăn một client làm buffer tăng vô hạn.

### Command được hỗ trợ

| Command | Ý nghĩa | Response điển hình |
|---|---|---|
| `SET key value` | Ghi value, bỏ TTL cũ | `+OK` |
| `GET key` | Đọc value | `$3` và body, hoặc `$-1` |
| `DEL key` | Xóa key | `:1` hoặc `:0` |
| `EXISTS key` | Kiểm tra tồn tại | `:1` hoặc `:0` |
| `EXPIRE key 10` | Hết hạn sau 10 giây | `:1` hoặc `:0` |
| `TTL key` | Số giây còn lại | số giây, `-1`, hoặc `-2` |
| `SAVE` | Ghi snapshot | `+OK` |
| `QUIT` | Đóng connection | `+BYE` |

Protocol tách token theo whitespace, nên value hiện **không chứa khoảng trắng**.
Đây là giới hạn của phiên bản học tập.

### Tự quan sát protocol

Terminal 1:

```bash
./build/edge_gateway --port 6379 --http-port 8080
```

Terminal 2:

```bash
nc localhost 6379
```

Sau đó gõ:

```text
SET temperature 31
GET temperature
EXPIRE temperature 10
TTL temperature
```

`nc` không biết protocol. Nó chỉ mở TCP connection, đọc bàn phím và gửi bytes.
Server mới là nơi diễn giải các bytes thành command.

## 7. HTTP và HTTP server

HTTP là **application protocol** thường chạy bên trên TCP. TCP vận chuyển bytes;
HTTP quy định ý nghĩa và định dạng của bytes đó.

Một HTTP/1.1 request gồm:

```text
PUT /kv/name HTTP/1.1
Host: localhost:8080
Content-Length: 3
<dòng trống>
Duy
```

Trên wire, mỗi dòng kết thúc bằng CRLF (hai byte carriage-return và line-feed),
và hai CRLF liên tiếp tạo dòng trống. Request gồm:

1. request line: method, target và version;
2. các header;
3. dòng trống kết thúc headers;
4. body có đúng số byte trong `Content-Length`.

### Route hiện có

| Method và path | Hành động | Status chính |
|---|---|---|
| `GET /health` | Kiểm tra process sống | `200 OK` |
| `PUT /kv/{key}` | Body trở thành value | `204 No Content` |
| `GET /kv/{key}` | Lấy value | `200` hoặc `404` |
| `DELETE /kv/{key}` | Xóa key | `204` hoặc `404` |

Method diễn tả hành động; path diễn tả resource; status code diễn tả kết quả.

### Luồng xử lý HTTP

```text
curl/browser -> TCP -> HttpServer::handle_client -> pending buffer
                                                     |
                                                 HttpParser
                                                     |
                                                   Request
                                                     |
                                             HttpServer::route
                                                     |
                                                KeyValueStore
                                                     |
                                  serialize Response -> send_all
```

`HttpServer::run()` vẫn tạo TCP socket, bind, listen và accept. HTTP server
không thay thế TCP/socket; nó là TCP server có parser theo quy tắc HTTP.

`HttpParser::parse()` trả một trong ba kết quả:

- `Incomplete`: chưa đủ bytes, cần `recv()` thêm;
- `ParseError`: request sai hoặc vượt giới hạn;
- `ParsedRequest`: có một request hoàn chỉnh và số bytes đã dùng.

Trường `consumed` cho phép xử lý khi buffer chứa hai request liên tiếp. Server
xóa request đầu khỏi `pending`, rồi parse request thứ hai. Project giới hạn
header 16 KiB và body 1 MiB để tránh client dùng vô hạn memory.

Project chỉ hỗ trợ HTTP/1.1 với `Content-Length`; chưa hỗ trợ chunked body,
TLS/HTTPS, timeout hoặc static file.

### Keep-alive

HTTP/1.1 có thể giữ một TCP connection để gửi nhiều request. Nếu header là
`Connection: close`, parser đặt `keep_alive = false`; server gửi response rồi
đóng connection. Keep-alive giảm chi phí tạo connection mới.

### Thử bằng curl

```bash
curl -v -X PUT --data 'Duy' http://localhost:8080/kv/name
curl -v http://localhost:8080/kv/name
curl -v -X DELETE http://localhost:8080/kv/name
curl -v http://localhost:8080/health
```

`-v` hiển thị request và response headers. `curl` chỉ là HTTP client; server
không phụ thuộc vào nó.

## 8. Hai server dùng chung state

Trong `apps/edge_gateway_main.cpp`, chỉ có **một** store:

```cpp
gateway::core::KeyValueStore store;
gateway::redis::TcpServer redis_server(..., store, snapshot);
gateway::http::HttpServer http_server(..., store);
```

Hai server giữ reference tới cùng object. Thử `SET color blue` bằng `nc`, rồi
chạy `curl http://localhost:8080/kv/color`; HTTP sẽ trả `blue`. Đây là vai trò
gateway: nhiều protocol adapter kết nối tới một core chung.

## 9. Thread pool và nhiều client

Nếu `run()` gọi thẳng `handle_client()`, server sẽ kẹt phục vụ một client và
không quay lại `accept()` sớm. Project dùng thread pool:

```text
accept loop                      worker threads
client A ---- submit task -----> worker 1: handle A
client B ---- submit task -----> worker 2: handle B
client C ---- submit task -----> queue -> worker rảnh
```

`ThreadPool` có task queue, mutex, condition variable và worker threads.
Destructor đặt cờ dừng, đánh thức worker và `join()` chúng. `join()` đảm bảo
thread kết thúc trước khi object liên quan bị hủy.

Project hiện có **một pool cho TCP server và một pool riêng cho HTTP server**,
dù cả hai nhận cùng giá trị `--threads`. Queue chưa có giới hạn, timeout hay
backpressure; đó là phần hardening sau này.

## 10. Vì sao KeyValueStore cần khóa?

Nhiều worker có thể đọc/ghi cùng `KeyValueStore`. `unordered_map` không an toàn
khi một thread sửa trong lúc thread khác truy cập. Project dùng
`std::shared_mutex`:

- `std::shared_lock`: nhiều reader cùng đọc;
- `std::unique_lock`: writer có quyền độc quyền.

`get()` ban đầu dùng shared lock. Nếu entry hết hạn, nó nhả shared lock rồi lấy
unique lock để xóa. Nó phải tìm lại entry vì thread khác có thể thay đổi map
trong khoảng chuyển lock.

TTL dùng `steady_clock` khi process chạy vì clock này không nhảy khi chỉnh giờ
hệ thống. Snapshot đổi thời điểm hết hạn sang `system_clock` để có ý nghĩa qua
lần restart.

## 11. Snapshot và graceful shutdown

State trong RAM mất khi process thoát. `SnapshotStore` ghi nó vào `gateway.db`:

```text
EDGE_GATEWAY_DB 1
"name" "Duy" -1
```

Giá trị cuối là thời điểm hết hạn; `-1` nghĩa là không hết hạn. Project ghi file
`.tmp` rồi rename thành file chính để giảm nguy cơ file chính bị ghi dở.

Khi nhận `SIGINT` (Ctrl+C) hoặc `SIGTERM`, `main`:

1. đặt cờ `interrupted`;
2. stop hai server;
3. shutdown socket để đánh thức `accept()` và `recv()`;
4. join hai server thread;
5. save snapshot;
6. hủy thread pool và join worker.

Đó là **graceful shutdown**: dừng có thứ tự, không bỏ mặc thread và resource.

## 12. MQTT là gì?

MQTT là application protocol theo mô hình **publish/subscribe**, thường chạy
trên TCP và có server trung tâm gọi là **broker**.

```text
publisher -- PUBLISH topic + payload --> broker
subscriber -- SUBSCRIBE topic --------> broker
subscriber <-- message ---------------- broker
```

Publisher không cần biết subscriber. Hai bên thống nhất topic, ví dụ
`gateway/kv/temperature`.

Khái niệm chính:

- **topic**: tên kênh phân cấp bằng `/`;
- **payload**: bytes của message;
- **QoS 0**: gửi một lần, không xác nhận;
- **QoS 1**: ít nhất một lần, có thể nhận trùng;
- **retained message**: broker giữ message cuối;
- **keep-alive/PING**: phát hiện connection hỏng;
- wildcard `+` và `#`: subscribe nhiều topic.

MQTT packet là binary và dùng trường **Remaining Length** để framing packet trên
TCP. Kiến trúc đích sẽ là:

```text
KeyValueStore/EventBus -> MQTT adapter -> TCP -> MQTT broker
```

Project chưa có MQTT client hoặc broker. Khi thêm, nên bắt đầu từ CONNECT,
PUBLISH QoS 0, SUBSCRIBE và PING, chưa nhảy ngay vào QoS 1/2.

## 13. SOME/IP là gì?

SOME/IP là protocol thường dùng trong automotive để các ECU/app giao tiếp theo
mô hình service. Các kiểu message quan trọng:

- **request/response**: gọi method và chờ kết quả;
- **fire-and-forget**: gửi request không cần response;
- **notification/event**: service phát sự kiện cho subscriber.

Binary header chứa Service ID, Method/Event ID, Length, Client ID, Session ID,
version, Message Type và Return Code. Số nhiều byte phải serialize theo
**network byte order** (big-endian), tương tự lý do project dùng `htons()`.

SOME/IP có thể dùng UDP hoặc TCP. SOME/IP Service Discovery giúp service thông
báo nó đang cung cấp gì và ở đâu, thường liên quan multicast UDP và state
machine riêng.

Khi hiện thực nên tách:

1. header serializer/parser, test bằng mảng bytes cố định;
2. transport UDP/TCP;
3. service discovery state machine.

Không trộn socket code vào serializer. Khi tách đúng, wire format có thể được
test không cần ECU hoặc network thật. Project hiện chưa có các phần này.

## 14. So sánh nhanh

| Khái niệm | Vai trò | Dữ liệu | Mô hình | Hiện có? |
|---|---|---|---|---|
| Socket | API hệ điều hành | byte I/O | endpoint | Có |
| TCP | transport | byte stream | connection | Có |
| Redis-like | application | text line | command/response | Có |
| HTTP/1.1 | application | header + body | request/response | Có |
| MQTT | application | binary packet | publish/subscribe | Chưa |
| SOME/IP | middleware | binary message | service/event | Chưa |

Cách nhớ ngắn:

```text
socket là API để code dùng network
TCP là cách bytes được vận chuyển đáng tin cậy
HTTP, MQTT và SOME/IP định nghĩa các bytes có nghĩa gì
```

## 15. Build, chạy và lỗi thường gặp

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/edge_gateway --port 6379 --http-port 8080 --db gateway.db --threads 4
```

- `--port`: Redis-like port;
- `--http-port`: HTTP port;
- `--db`: đường dẫn snapshot;
- `--threads`: số worker **cho mỗi server pool**.

`Address already in use`: process khác đang giữ port hoặc instance cũ chưa tắt.

`Connection refused`: server chưa chạy, đã lỗi hoặc client dùng sai port.

Kết nối nhưng không có response: command TCP chưa có newline; HTTP chưa có dòng
trống kết thúc headers; hoặc `Content-Length` lớn hơn body nên parser còn đợi.

HTTP `400`: request line/header sai, dùng HTTP/1.0, hoặc `Content-Length` không
hợp lệ.

## 16. Thứ tự đọc code đề xuất

1. `tests/gateway_tests.cpp`: hành vi mong muốn.
2. `key_value_store.hpp/.cpp`: core state không có network.
3. `command_parser.hpp/.cpp`: text thành typed command.
4. `tcp_server.cpp::handle_client`: TCP stream và framing.
5. `tcp_server.cpp::run`: socket lifecycle.
6. `http_parser.hpp/.cpp`: incremental parser và Content-Length.
7. `http_server.cpp`: routing và serialize response.
8. `thread_pool.hpp`: concurrency.
9. `snapshot_store.cpp`: persistence.
10. `edge_gateway_main.cpp`: ghép component và lifecycle.

Ở mỗi bước, hãy hỏi: input là gì, output là gì, ai sở hữu object, và nếu dữ liệu
chỉ đến một nửa thì chuyện gì xảy ra?

## 17. Bài thực hành

### Mức 1: quan sát

1. Ghi bằng TCP và đọc bằng HTTP.
2. Ghi bằng HTTP và đọc bằng TCP.
3. Đặt TTL 5 giây, đọc trước và sau khi hết hạn.
4. Ctrl+C, chạy lại và kiểm tra snapshot.
5. Dùng `curl -v` quan sát headers.

### Mức 2: sửa nhỏ

1. Thêm `GET /metrics` trả số key.
2. Thêm command `PING` trả `+PONG`.
3. Thêm test parser cho HTTP body chưa đến đủ.
4. Trả header `Allow` khi status là `405`.

### Mức 3: hiểu framing

1. Viết client gửi nửa đầu command, chờ, rồi gửi nửa sau.
2. Gửi hai command trong một lần write.
3. Chứng minh code không giả định một `recv` là một message.
4. Thiết kế length-prefixed value chứa whitespace/newline.

### Mức 4: protocol mới

1. Encode/decode MQTT Remaining Length và test boundary.
2. Parse MQTT fixed header trước khi mở socket.
3. Viết SOME/IP header serializer/parser độc lập transport.
4. Test network byte order bằng expected byte array.

## 18. Checklist tự kiểm tra

Bạn đã nắm phần hiện tại khi có thể giải thích:

- IP và port khác nhau thế nào?
- listening socket khác client socket thế nào?
- vì sao một `recv()` không tương ứng một request?
- hai protocol hiện tại tìm biên message bằng gì?
- vì sao `send_all()` phải loop?
- vì sao nhiều worker cần khóa store?
- shutdown socket giúp thread thoát thế nào?
- vì sao HTTP và TCP nhìn thấy cùng dữ liệu?
- MQTT khác HTTP về mô hình giao tiếp thế nào?
- vì sao serializer SOME/IP nên tách khỏi UDP/TCP?

Networking dễ hiểu hơn nhiều khi quan sát bytes thật thay vì chỉ ghi nhớ thuật
ngữ. Nếu chưa trả lời được câu nào, quay lại mục tương ứng và chạy ví dụ nhỏ.
