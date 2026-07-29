# UML trực quan cho C++ Edge Gateway

Tài liệu này biến source code thành các hình dễ đọc. Không cần thuộc ký hiệu UML
trước. Hãy bắt đầu bằng câu hỏi ghi ngay trên mỗi sơ đồ.

## 1. Cách đọc sơ đồ

| Ký hiệu | Cách hiểu |
|---|---|
| Hộp | Một component, class, process hoặc object |
| Mũi tên `A → B` | A gọi, gửi dữ liệu hoặc phụ thuộc B |
| Nét thời gian từ trên xuống | Việc phía trên xảy ra trước việc phía dưới |
| `*--` trong class diagram | Sở hữu mạnh; object con sống cùng object cha |
| `o--` | Giữ/tham chiếu nhưng lifecycle có thể độc lập |
| `..>` | Chỉ sử dụng, không sở hữu |

Một sơ đồ không thể trả lời mọi câu hỏi. Component diagram cho biết các khối
liên hệ thế nào; class diagram cho biết cấu trúc code; sequence diagram cho biết
thứ tự chạy; deployment diagram cho biết process nằm ở máy nào.

## 2. Toàn hệ thống — dữ liệu đi đâu?

Câu hỏi cần trả lời: “Một request từ bên ngoài vào gateway rồi đi ra protocol
khác như thế nào?”

```mermaid
flowchart LR
    subgraph External["Hệ thống bên ngoài"]
        CLI["TCP client"]
        WEB["HTTP client"]
        DEVICE["MQTT device"]
        ECU["SOME/IP ECU"]
        BROKER["MQTT broker"]
    end

    subgraph Gateway["edge_gateway process"]
        TCP["CommandServer<br/>Boost.Asio"]
        HTTP["HttpServer<br/>Boost.Beast"]
        MQTT["MqttBridge<br/>Paho"]
        SIP["VSomeIpService<br/>vsomeip"]

        STORE[("StateStore<br/>business state")]
        BUS["StateEventBus<br/>domain events"]
        FILE[("gateway.db<br/>snapshot")]
    end

    CLI -->|"SET / GET / DEL"| TCP
    WEB -->|"HTTP request"| HTTP
    DEVICE -->|"PUBLISH command"| BROKER
    BROKER -->|"subscribed message"| MQTT
    ECU -->|"method request"| SIP

    TCP -->|"set / get / erase"| STORE
    HTTP -->|"set / get / erase"| STORE
    MQTT -->|"set"| STORE
    SIP -->|"set / get"| STORE

    STORE -->|"StateChanged"| BUS
    BUS -->|"publish state"| MQTT
    MQTT -->|"PUBLISH event"| BROKER
    BUS -->|"notify field"| SIP
    SIP -->|"notification"| ECU
    FILE <-->|"load lúc start<br/>save lúc stop"| STORE
```

Điểm cần nhớ:

- Mọi input adapter hội tụ tại `StateStore`.
- `StateStore` không gửi MQTT hay SOME/IP trực tiếp; nó phát `StateChanged`.
- Output adapter subscribe `StateEventBus` rồi chuyển domain event sang
  protocol tương ứng.
- Broker nằm ngoài gateway. `MqttBridge` chỉ là MQTT client.

## 3. Các tầng phụ thuộc — vì sao domain không include middleware?

Câu hỏi cần trả lời: “Code tầng nào được phép biết tầng nào?”

```mermaid
flowchart TB
    APP["apps/edge_gateway_main.cpp<br/>Composition root"]
    ADAPTER["adapters<br/>TCP · HTTP · MQTT · SOME/IP"]
    INFRA["infrastructure<br/>FileSnapshotRepository"]
    APPLICATION["application<br/>StateWireCodec"]
    DOMAIN["domain<br/>StateStore · StateEventBus · StateChanged"]
    LIBS["Libraries<br/>Asio · Beast · Paho · vsomeip"]

    APP --> ADAPTER
    APP --> INFRA
    ADAPTER --> APPLICATION
    ADAPTER --> DOMAIN
    ADAPTER --> LIBS
    INFRA --> DOMAIN
```

Mũi tên là hướng phụ thuộc source code. Domain nằm trong cùng và không biết
Boost, Paho hay vsomeip. `application/StateWireCodec` cũng độc lập với
middleware. Vì thế core test chạy được mà không cần network.

## 4. Class diagram — ai sở hữu và gọi ai?

Câu hỏi cần trả lời: “Các class chính liên hệ với nhau thế nào?”

```mermaid
classDiagram
    direction LR

    class StateChanged {
        +string key
        +optional~string~ value
        +ChangeOrigin origin
    }

    class StateEventBus {
        -unordered_map handlers
        -mutex mutex
        +subscribe(handler) Subscription
        +publish(event)
    }

    class Subscription {
        -StateEventBus* bus
        -size_t id
        +reset()
        +~Subscription()
    }

    class StateStore {
        -unordered_map entries
        -shared_mutex mutex
        -StateEventBus& events
        +set(key, value, origin)
        +get(key) optional~string~
        +erase(key, origin) bool
        +expire(key, ttl) bool
        +snapshot() vector
    }

    class CommandServer {
        -tcp_acceptor acceptor
        -StateStore& store
        +start()
        +stop()
    }

    class HttpServer {
        -tcp_acceptor acceptor
        -StateStore& store
        +start()
        +stop()
    }

    class MqttBridge {
        -mqtt_async_client client
        -StateStore& store
        -Subscription subscription
        +start()
        +stop()
        -message_arrived(message)
        -publish_change(event)
    }

    class VSomeIpService {
        -vsomeip_application application
        -StateStore& store
        -Subscription subscription
        +run()
        +stop()
        -on_get(request)
        -on_set(request)
        -notify_change(event)
    }

    class StateWireCodec {
        +encode_key_value(key, value) bytes
        +decode_key(bytes) optional
        +decode_key_value(bytes) optional
    }

    class FileSnapshotRepository {
        -path database_path
        +load(store)
        +save(store)
    }

    StateStore o-- StateEventBus : publishes to
    StateEventBus *-- Subscription : creates RAII token
    StateEventBus ..> StateChanged : transports
    CommandServer --> StateStore
    HttpServer --> StateStore
    MqttBridge --> StateStore
    MqttBridge o-- Subscription
    VSomeIpService --> StateStore
    VSomeIpService o-- Subscription
    VSomeIpService ..> StateWireCodec
    FileSnapshotRepository ..> StateStore
```

`Subscription` là RAII token: khi token bị hủy/reset, handler được gỡ khỏi
event bus. Điều này ngăn bus callback vào adapter đã bị hủy.

## 5. TCP sequence — một command thực sự được đọc thế nào?

Câu hỏi cần trả lời: “Vì sao TCP cần newline framing?”

```mermaid
sequenceDiagram
    autonumber
    actor Client
    participant OS as TCP stack
    participant Acceptor as CommandServer
    participant Session
    participant Store as StateStore

    Acceptor->>OS: async_accept()
    Client->>OS: connect port 6379
    OS-->>Acceptor: connection socket
    Acceptor->>Session: create(socket)
    Session->>OS: async_read_until(newline)

    Note over Client,OS: Client gửi hai lần<br/>"SET rpm " rồi "2500\n"
    Client->>OS: bytes "SET rpm "
    Client->>OS: bytes "2500\n"
    Note over OS,Session: TCP có thể giao thành một<br/>hoặc nhiều read fragment
    OS-->>Session: đủ byte đến newline

    Session->>Session: parse "SET rpm 2500"
    Session->>Store: set("rpm", "2500", tcp)
    Store-->>Session: return
    Session->>OS: async_write("+OK\r\n")
    OS-->>Client: response bytes
    Session->>OS: async_read_until(newline)
```

Thông tin phải nhớ: hai lần client `send` không bắt buộc thành hai lần server
`read`. `async_read_until` tạo message boundary bằng newline.

## 6. HTTP sequence — HTTP nằm trên TCP ở đâu?

Câu hỏi cần trả lời: “TCP, Beast, adapter và domain chia việc ra sao?”

```mermaid
sequenceDiagram
    autonumber
    actor Client
    participant TCP as OS TCP stream
    participant Beast
    participant HTTP as HttpServer
    participant Store as StateStore
    participant Bus as StateEventBus

    Client->>TCP: PUT /kv/rpm + headers + body
    TCP->>Beast: ordered byte stream
    Beast->>Beast: parse start-line, headers, framing
    Beast->>HTTP: typed request
    HTTP->>HTTP: validate method and route
    HTTP->>Store: set("rpm", "2500", http)
    Store->>Bus: publish StateChanged
    Store-->>HTTP: complete
    HTTP->>Beast: typed 200 response
    Beast->>TCP: serialize HTTP bytes
    TCP-->>Client: HTTP/1.1 200 OK
```

TCP chỉ bảo toàn dòng byte. Beast hiểu cú pháp HTTP. `HttpServer` hiểu route và
mapping nghiệp vụ. `StateStore` không biết request đến từ Beast.

## 7. MQTT sequence — broker đứng ở đâu?

Câu hỏi cần trả lời: “Command MQTT đi vào và state event đi ra thế nào?”

```mermaid
sequenceDiagram
    autonumber
    actor Device
    participant Broker
    participant Paho
    participant Bridge as MqttBridge
    participant Store as StateStore
    participant Bus as StateEventBus

    Bridge->>Paho: start and connect
    Paho->>Broker: CONNECT
    Broker-->>Paho: CONNACK
    Paho->>Broker: SUBSCRIBE gateway/command/#

    Device->>Broker: PUBLISH gateway/command/fan = on
    Broker->>Paho: PUBLISH
    Paho->>Bridge: message_arrived()
    Bridge->>Store: set("fan", "on", mqtt)
    Store->>Bus: StateChanged
    Bus->>Bridge: publish_change(event)
    Bridge->>Paho: publish gateway/state/fan
    Paho->>Broker: PUBLISH state event
```

Broker định tuyến theo topic. `StateEventBus` định tuyến domain event trong
process. Hai thành phần cùng có ý tưởng phân phối, nhưng không phải một thứ.

## 8. SOME/IP sequence — discovery, method và event

Câu hỏi cần trả lời: “Offer, request/response và subscribe khác nhau ở đâu?”

```mermaid
sequenceDiagram
    autonumber
    participant ECU as Client ECU
    participant SD as SOME/IP-SD
    participant VS as vsomeip runtime
    participant Service as VSomeIpService
    participant Store as StateStore

    Service->>VS: init() then start()
    VS-->>Service: ST_REGISTERED
    Service->>VS: offer_event(E=0x8001, group=0x0001)
    Service->>VS: offer_service(S=0x1234, I=0x5678)
    VS->>SD: OfferService + endpoint + TTL
    SD-->>ECU: service is available

    ECU->>VS: SOME/IP SET request<br/>client ID + session ID
    VS->>Service: on_set(request)
    Service->>Service: decode key/value payload
    Service->>Store: set(key, value, someip)
    Service->>VS: create_response(request) + send
    VS-->>ECU: response with matching IDs

    ECU->>SD: Subscribe Eventgroup 0x0001
    SD-->>ECU: Subscribe ACK
    Store-->>Service: StateChanged callback
    Service->>VS: notify Event 0x8001
    VS-->>ECU: SOME/IP notification
```

Ba pha độc lập:

1. SD giúp client tìm endpoint.
2. SOME/IP request/response thực hiện method.
3. Event chỉ đến sau khi client subscribe đúng eventgroup.

## 9. Deployment diagram — localhost khác hai ECU thế nào?

Câu hỏi cần trả lời: “Component chạy ở process/máy nào?”

```mermaid
flowchart LR
    subgraph ECU_A["ECU A / Gateway host"]
        subgraph PROC["edge_gateway process"]
            GW["Adapters + Domain"]
            VSAPP["vsomeip application"]
        end
        ROUTER["vsomeip routing manager"]
        DB[("gateway.db")]
    end

    subgraph BROKER_HOST["Broker host"]
        MOSQ["Mosquitto broker<br/>TCP 1883"]
    end

    subgraph ECU_B["ECU B / SOME-IP client"]
        CLIENT["Client application"]
        VSC["vsomeip runtime"]
    end

    GW <-->|"local API"| VSAPP
    VSAPP <-->|"local IPC"| ROUTER
    GW <-->|"file I/O"| DB
    GW <-->|"MQTT over TCP"| MOSQ
    ROUTER <-->|"SOME/IP-SD<br/>UDP multicast 30490"| VSC
    ROUTER <-->|"SOME/IP data<br/>UDP 30509"| VSC
    CLIENT <-->|"vsomeip API"| VSC
```

Trong config hiện tại, gateway cũng là routing manager và dùng loopback. Khi
chạy hai ECU thật, multicast route, interface, firewall và unicast address mới
tham gia. Local IPC pass không chứng minh remote network đã đúng.

## 10. Thread model — callback có thể chạy ở đâu?

Câu hỏi cần trả lời: “Vì sao domain phải thread-safe?”

```mermaid
flowchart TB
    MAIN["Main thread<br/>compose · purge TTL · shutdown"]

    subgraph ASIO["Asio worker threads (N)"]
        TCP_CB["TCP accept/read/write callbacks"]
        HTTP_CB["HTTP accept/read/write callbacks"]
    end

    subgraph PAHO["Paho-managed thread(s)"]
        MQTT_IN["connect/message callbacks"]
        PAHO_API["Paho client API"]
    end

    subgraph VSOMEIP["Dedicated vsomeip thread"]
        SIP_IN["start dispatch loop<br/>method/state callbacks"]
        VS_API["vsomeip application API"]
    end

    STORE[("StateStore<br/>shared_mutex")]
    BUS["StateEventBus<br/>mutex + copied handlers"]
    MQTT_OUT["MqttBridge::publish_change<br/>runs on publishing thread"]
    SIP_OUT["VSomeIpService::notify_change<br/>runs on publishing thread"]

    MAIN -->|"purge_expired"| STORE
    TCP_CB --> STORE
    HTTP_CB --> STORE
    MQTT_IN --> STORE
    SIP_IN --> STORE
    STORE --> BUS
    BUS -.->|"synchronous callback"| MQTT_OUT
    BUS -.->|"synchronous callback"| SIP_OUT
    MQTT_OUT --> PAHO_API
    SIP_OUT --> VS_API
```

`StateEventBus::publish` gọi handler đồng bộ trên chính thread đã thay đổi
state; nó không tự chuyển callback sang Paho/vsomeip thread. `StateStore` cần
`shared_mutex`; event bus copy danh sách handler rồi nhả mutex trước khi gọi để
tránh giữ khóa qua middleware.

## 11. Startup và shutdown

Câu hỏi cần trả lời: “Thứ tự lifecycle bảo vệ điều gì?”

```mermaid
sequenceDiagram
    autonumber
    participant Main
    participant File as Snapshot
    participant Store
    participant TCPHTTP as TCP and HTTP
    participant MQTT
    participant SOMEIP
    participant Workers

    Main->>File: load(store)
    File->>Store: restore(records)
    Main->>TCPHTTP: start()
    Main->>MQTT: start()
    Main->>SOMEIP: run() on dedicated thread
    Main->>Workers: run io_context on N threads

    Note over Main,Workers: phục vụ đến khi SIGINT hoặc SIGTERM

    Main->>SOMEIP: stop()
    Main->>MQTT: stop()
    Main->>TCPHTTP: stop()
    Main->>Workers: stop io_context and join
    Main->>SOMEIP: join thread
    Main->>File: save(store)
```

Input được dừng trước khi snapshot cuối được lưu, nên file phản ánh state sau
cùng. Subscription được reset trước khi middleware object dừng để giảm nguy cơ
callback dùng object đang teardown.

## 12. Một state update qua ba kiểu sơ đồ

Cùng sự kiện `HTTP PUT /kv/rpm`, hãy nhớ bằng ba góc nhìn:

```mermaid
flowchart LR
    STRUCTURE["Structure<br/>HttpServer → StateStore → EventBus"]
    BEHAVIOR["Behavior<br/>request → set → publish → response"]
    DEPLOYMENT["Deployment<br/>client process → gateway process"]

    STRUCTURE ---|"cùng một hệ thống"| BEHAVIOR
    BEHAVIOR ---|"cùng một hệ thống"| DEPLOYMENT
```

- Không dùng class diagram để suy ra thứ tự thời gian.
- Không dùng sequence diagram để suy ra ownership.
- Không dùng deployment diagram để suy ra business semantics.

Khi tự vẽ được cả ba góc nhìn cho TCP, HTTP, MQTT và SOME/IP, bạn đã hiểu hệ
thống thay vì chỉ nhớ tên class.

## 13. Bài tập đọc UML

1. Chỉ trên component diagram nơi MQTT broker nằm và giải thích vì sao nó không
   thuộc process gateway.
2. Chỉ trên class diagram object nào giữ RAII subscription.
3. Dùng TCP sequence giải thích message boundary.
4. Dùng SOME/IP sequence phân biệt discovery với data exchange.
5. Dùng deployment diagram giải thích vì sao localhost pass chưa đủ.
6. Dùng thread diagram tìm tất cả nguồn có thể đồng thời gọi `StateStore`.
7. Tự thêm luồng `TCP SET → MQTT publish` vào một sequence diagram mới.
