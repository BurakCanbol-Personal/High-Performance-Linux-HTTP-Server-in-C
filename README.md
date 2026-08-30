# High-Performance Linux HTTP Server in C

A lightweight HTTP server written from scratch in C using Linux sockets, non-blocking I/O, and `epoll`.

I built this project as a learning project to better understand how high-performance network servers work underneath frameworks and higher-level programming languages.

Rather than using an HTTP or networking library, the project directly uses Linux system calls such as:

- `socket()`
- `bind()`
- `listen()`
- `accept()`
- `recv()`
- `send()`
- `fcntl()`
- `epoll_create1()`
- `epoll_ctl()`
- `epoll_wait()`

The final server uses **non-blocking sockets and edge-triggered epoll (`EPOLLET`)** to handle multiple client connections using a single event loop.

---

## What I Learned

This project was developed incrementally.

I first implemented a basic blocking TCP server and then gradually redesigned it into an event-driven server.

The main progression was:

```text
Basic TCP Server
      ↓
Blocking accept / recv
      ↓
HTTP request + response
      ↓
Thread-per-client using pthreads
      ↓
Non-blocking sockets
      ↓
epoll event loop
      ↓
Per-client connection state
      ↓
Partial read/write handling
      ↓
Edge-triggered epoll
```

This helped me understand why high-performance servers are designed differently from simple blocking servers.

---

# 1. Basic TCP Server

The first version created a TCP socket using:

```c
socket(AF_INET, SOCK_STREAM, 0);
```

The server then:

1. created a socket
2. bound it to port `18080`
3. started listening
4. accepted a client connection
5. received data
6. sent an HTTP response
7. closed the connection

The server listens on:

```text
0.0.0.0:18080
```

`INADDR_ANY` allows the socket to accept connections through any local network interface.

---

# 2. File Descriptors

Linux represents sockets using **file descriptors**.

For example:

```text
0 → stdin
1 → stdout
2 → stderr
3 → listening socket
5 → connected client
```

The listening socket and connected client sockets have different purposes.

```text
server_fd
   │
   │ accept()
   ▼
client_fd
```

`server_fd` waits for new connections.

Each `client_fd` represents an established TCP connection that can be used with `recv()` and `send()`.

---

# 3. HTTP

The server manually handles a simple HTTP request.

Example request:

```http
GET / HTTP/1.1
Host: 127.0.0.1:18080
```

The server responds with:

```http
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Length: 13
Connection: close

Hello from C!
```

No HTTP library is used.

The response is constructed manually as a C string.

---

# 4. The Blocking Problem

The original server used blocking sockets.

A call such as:

```c
recv(client_fd, ...);
```

could cause the server to wait for one client.

For example:

```text
Client A connects
      ↓
server waits inside recv()
      ↓
Client B connects
      ↓
Client B must wait
```

This showed why a simple blocking design does not scale well when many clients are connected.

---

# 5. Thread-per-Client Version

The next version used POSIX threads (`pthread`).

Each accepted connection was given to a worker thread:

```text
             Server
                │
        ┌───────┼───────┐
        ▼       ▼       ▼
     Thread   Thread   Thread
        │       │       │
     Client A Client B Client C
```

This allowed multiple clients to be processed concurrently.

It also demonstrated some important concepts:

- thread creation
- detached threads
- ownership of file descriptors
- passing data to worker threads
- avoiding races when passing `client_fd`

However, creating one thread for every connection can become expensive with a large number of clients.

This motivated the move to an event-driven architecture.

---

# 6. Non-Blocking Sockets

The server sockets were changed to non-blocking mode using `fcntl()`:

```c
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

With non-blocking sockets, operations do not wait indefinitely.

For example, when there is currently nothing available:

```c
recv(...)
```

can return:

```text
EAGAIN
```

or:

```text
EWOULDBLOCK
```

This means:

> There is nothing available right now. Try again later.

Non-blocking sockets allow one thread to manage many connections without becoming stuck on one client.

---

# 7. epoll

The final server uses Linux `epoll`.

An epoll instance is created with:

```c
epoll_create1(0);
```

Sockets are registered using:

```c
epoll_ctl();
```

The server then waits for activity using:

```c
epoll_wait();
```

Conceptually:

```text
                  epoll
                    │
        ┌───────────┼───────────┐
        │           │           │
    server_fd    client 5    client 6
        │           │           │
   connection      data        data
      ready        ready       ready
```

Instead of checking every socket continuously, the program asks the Linux kernel to notify it when a socket becomes ready.

---

# 8. EPOLLIN and EPOLLOUT

Two important epoll events are used.

### EPOLLIN

```c
EPOLLIN
```

means the socket is ready to be read.

For the listening socket, this usually means:

```text
A new connection is waiting.
```

For a client socket:

```text
Client data is available to recv().
```

### EPOLLOUT

```c
EPOLLOUT
```

means the socket is ready for writing.

After a complete HTTP request is received, the server changes the client's epoll interest from:

```text
EPOLLIN
```

to:

```text
EPOLLOUT
```

using:

```c
EPOLL_CTL_MOD
```

The flow becomes:

```text
EPOLLIN
   ↓
receive request
   ↓
request complete
   ↓
prepare response
   ↓
EPOLL_CTL_MOD
   ↓
EPOLLOUT
   ↓
send response
```

---

# 9. Per-Client State

TCP communication may require multiple reads and writes.

For that reason, each connection stores its own state:

```c
struct client_state {
    int fd;

    char request[BUFFER_SIZE];
    size_t request_len;

    const char *response;
    size_t response_len;
    size_t response_sent;
};
```

The structure allows the server to remember the progress of every client between calls to `epoll_wait()`.

The pointer is stored inside:

```c
epoll_event.data.ptr
```

so an epoll event can directly identify the corresponding client's state.

Conceptually:

```text
epoll event
    │
    ▼
data.ptr
    │
    ▼
client_state
    ├── fd
    ├── request
    ├── request_len
    ├── response
    ├── response_len
    └── response_sent
```

---

# 10. TCP Is a Byte Stream

One important lesson from this project is that:

```text
1 recv() != 1 HTTP request
```

TCP is a byte stream.

A request might arrive like this:

```text
recv #1 → "GET / HTTP/1.1\r\n"

recv #2 → "Host: 127.0.0.1:18080\r\n"

recv #3 → "\r\n"
```

The server therefore stores incoming data in the client's request buffer:

```c
client->request + client->request_len
```

and keeps track of the total amount received.

For this simple HTTP server, a complete header is detected using:

```c
strstr(client->request, "\r\n\r\n")
```

because an HTTP header section ends with:

```text
\r\n\r\n
```

This was tested by intentionally sending a request in multiple pieces using `nc`.

---

# 11. Partial Writes

The same problem exists when sending.

A call to:

```c
send()
```

is not guaranteed to send the entire response.

For example:

```text
Response: 10,000 bytes

send() → 4,000
send() → 3,000
send() → 3,000
```

The server therefore keeps track of:

```c
response_sent
```

and continues from:

```c
client->response + client->response_sent
```

until:

```c
response_sent == response_len
```

Only then is the connection closed.

---

# 12. Edge-Triggered epoll

The final version uses:

```c
EPOLLET
```

which enables **edge-triggered epoll**.

The difference between level-triggered and edge-triggered behavior is important.

### Level-triggered

Linux repeatedly reports that a socket is ready as long as data remains available.

```text
"Still ready"
"Still ready"
"Still ready"
```

### Edge-triggered

Linux mainly informs the application when the socket changes from:

```text
not ready → ready
```

Conceptually:

```text
"It just became ready."
```

Because of this, the server drains operations until they return `EAGAIN`.

For accepting connections:

```text
accept()
accept()
accept()
...
EAGAIN
```

For receiving data:

```text
recv()
recv()
recv()
...
EAGAIN
```

For sending:

```text
send()
send()
send()
...
EAGAIN
```

This pattern is particularly important when using non-blocking sockets with edge-triggered epoll.

---

# Architecture

The final architecture looks approximately like this:

```text
                    Linux Kernel
                         │
                         │
                    epoll_wait()
                         │
             ┌───────────┴───────────┐
             │                       │
         server_fd               client event
             │                       │
          EPOLLIN             ┌──────┴──────┐
             │                │             │
      accept clients       EPOLLIN       EPOLLOUT
             │                │             │
       until EAGAIN        recv()         send()
                           │               │
                     until EAGAIN    until EAGAIN
                           │               │
                     complete HTTP    response done
                        request            │
                           │             close
                        EPOLLOUT
```

The server can therefore manage multiple connections using a **single event loop without creating one thread for every client**.

---

# Building

This project is intended to run on Linux.

I developed and tested it using WSL2 Ubuntu on Windows.

Compile with GCC:

```bash
gcc -Wall -Wextra -Wpedantic src/main.c -o build/server
```

Run:

```bash
./build/server
```

The server listens on port:

```text
18080
```

---

# Testing

Using `curl`:

```bash
curl http://127.0.0.1:18080/
```

Expected output:

```text
Hello from C!
```

A typical server log looks like:

```text
Socket created successfully. fd = 3
Socket bound successfully to port 18080.
Server listening on port 18080...

Incoming connection ready.
Client accepted. fd = 5
Client fd 5 added to epoll.

Client fd 5 sent a complete request:
GET / HTTP/1.1
Host: 127.0.0.1:18080

fd 5: sent 97 bytes, total 97/97
Response completely sent to fd 5.
```

---

# Testing Partial TCP Reads

To verify that the server does not assume one `recv()` contains the complete HTTP request:

```bash
{
    printf 'GET / HTTP/1.1\r\n'
    sleep 2
    printf 'Host: 127.0.0.1:18080\r\n'
    sleep 2
    printf '\r\n'
} | nc 127.0.0.1 18080
```

This sends the HTTP request in several pieces.

The server correctly accumulates them:

```text
fd 5: received 16 bytes, total = 16
fd 5: received 23 bytes, total = 39

Client fd 5 sent a complete request:
GET / HTTP/1.1
Host: 127.0.0.1:18080
```

and only sends a response after the request becomes complete.

---

# Inspecting the Server with strace

The server can also be inspected using Linux `strace`.

For example:

```bash
strace -f -e trace=network,process ./build/server
```

This makes it possible to see the underlying system calls performed by the server, such as:

```text
socket()
bind()
listen()
accept()
recvfrom()
sendto()
```

During the earlier pthread implementation, `strace` also showed the Linux `clone()` system call used when creating threads.

This helped connect the C functions used by the program with what actually happens inside Linux.

---

# Current Limitations

This project is primarily an educational systems-programming project, not a production-ready HTTP server.

The current implementation intentionally keeps HTTP functionality simple.

For example, it does not yet provide:

- a complete HTTP parser
- multiple routes
- request body handling
- HTTP keep-alive
- chunked transfer encoding
- TLS/HTTPS
- advanced error responses
- production security hardening
- multi-core event loops

The current request-completion check mainly handles simple HTTP requests without request bodies.

---

# Possible Future Improvements

Potential extensions include:

- proper HTTP request parsing
- request size limits
- `EPOLLERR`, `EPOLLHUP`, and `EPOLLRDHUP` handling
- graceful `SIGINT` shutdown
- `SIGPIPE` handling
- `SO_REUSEADDR`
- `accept4()` with `SOCK_NONBLOCK`
- HTTP keep-alive
- static file serving
- `sendfile()`
- multi-core worker architecture
- benchmarking with `wrk`
- profiling using `perf`
- packet inspection using `tcpdump`
- syscall analysis using `strace`

---

# Why I Built This

High-level backend frameworks hide much of the networking infrastructure underneath them.

For example, an application may eventually depend on concepts such as:

```text
HTTP framework
      ↓
event loop
      ↓
non-blocking I/O
      ↓
epoll
      ↓
Linux sockets
      ↓
TCP
```

I built this project to understand those lower layers directly rather than only using them through frameworks.

The main goal was not to build a complete web framework, but to understand the systems concepts behind scalable network servers.

---

## Status

Core learning project: **Completed**

Current server:

```text
C
+ Linux sockets
+ non-blocking I/O
+ epoll
+ EPOLLIN / EPOLLOUT
+ per-client state
+ partial reads/writes
+ edge-triggered events
```

The next stage of the project is performance testing and analysis using Linux profiling and networking tools.