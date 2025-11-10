 ---        
  Load Balancer C++ — Full Project Audit                                                                                                                                                                                          
                                  
  Here's every issue I found, organized by severity. This is honest — some are critical bugs, some are code quality, some are resume optics.                                                                                      
                                                                                                                                                                                                                                  
  ---                                                                                                                                                                                                                             
  Critical Bugs (will cause crashes or incorrect behavior)                                                                                                                                                                        
                                                                                                                                                                                                                                  
  1. TOCTOU Race Condition — Least Connections Is Broken                                                                                                                                                                          
                                                                                                                                                                                                                                  
  File: load-balancer/LoadBalancer.cpp:14-22                                                                                                                                                                                      

  // BUG: find releases lock, then handle_client re-acquires to increment                                                                                                                                                         
  int index = find_least_connection_index();   // lock acquired, then RELEASED                                                                                                                                                    
  ...                                                                                                                                                                                                                             
  active_connections[index]++;                  // lock re-acquired — too late!                                                                                                                                                   
  Between these two locks, another thread picks the same "least loaded" backend and both increment it. The least-connections algorithm is entirely defeated under any concurrency. Fix: hold the lock across both the selection
  AND the increment atomically.

  ---
  2. Active Connections Leak on Backend Failure

  File: load-balancer/LoadBalancer.cpp:29-36

  If connect() to backend fails, the function returns without decrementing active_connections[index], which was already incremented. That backend will accumulate phantom connections and never receive traffic again. This is a
  permanent counter leak.

  ---
  3. Broken Proxy Loop — Deadlock + Data Loss

  File: load-balancer/LoadBalancer.cpp:38-43

  while ((bytes_read = read(client_socket, buffer, sizeof(buffer))) > 0) {
      send(backend_socket, buffer, bytes_read, 0);
      bytes_read = read(backend_socket, buffer, sizeof(buffer));  // ← blocks forever if backend stalls
      send(client_socket, buffer, bytes_read, 0);                // ← called even if bytes_read == -1
  }
  Four bugs here:
  - Sequential read blocks — if backend never replies, the thread hangs forever
  - bytes_read from backend is never checked for -1/0 before being passed to send()
  - send() return values are never checked — partial sends silently drop data
  - Buffer is 1024 bytes; any HTTP response larger than 1KB is silently truncated/corrupted

  The correct approach is select()/epoll() bidirectional forwarding or two threads per connection.

  ---
  4. Data Race on active_connections Read After Lock Release

  File: load-balancer/LoadBalancer.cpp:50-54

  {
      std::lock_guard<std::mutex> lock(connection_mutex);
      active_connections[index]--;
  }  // lock released here
  // ↓ another thread can modify active_connections[index] RIGHT HERE
  std::cout << "Active connections: " << active_connections[index] << "\n";
  Undefined behavior — reading shared state outside the lock.

  ---
  5. No SO_REUSEADDR on Server Socket

  File: load-balancer/LoadBalancer.cpp:58

  If the load balancer crashes and restarts, bind() will fail with "Address already in use" for up to 60 seconds (TCP TIME_WAIT). For any production use this is a show-stopper. One line fix: setsockopt(server_socket,
  SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)).

  ---
  6. Uninitialized sockaddr_in Structs

  File: load-balancer/LoadBalancer.cpp:26, 60

  sockaddr_in backend_addr and server_addr are declared on the stack but never memset to zero. The padding bytes contain garbage, which can cause bind()/connect() to fail or behave unpredictably on some platforms.

  ---
  Major Design Flaws

  7. CMakeLists.txt Contains Makefile Syntax

  File: load-balancer/CMakeLists.txt

  This file is literally a copy-paste of Makefile. CMake syntax is completely different (cmake_minimum_required, project(), add_executable(), etc.). Anyone trying to build with CMake will get a parse error. Either write real
  CMake or delete this file.

  ---
  8. Thread-Per-Connection — Unbounded Thread Explosion

  File: load-balancer/LoadBalancer.cpp:79

  std::thread(&LoadBalancer::handle_client, this, client_socket).detach();
  Every single client connection spawns a new OS thread. Under any real load (e.g., 1000 concurrent connections) this will exhaust system resources and crash. A thread pool (e.g., using a work queue + fixed N threads) is the
  standard fix and is a fundamental systems programming concept.

  ---
  9. No Health Checking

  If any backend goes down, the load balancer keeps routing traffic to it, causing all those connections to fail. There's no periodic health check (TCP connect probe or HTTP HEAD request) to mark backends as unavailable.

  ---
  10. No Connection/Read Timeout

  read(client_socket, ...) blocks indefinitely. A slow/stalled client holds a thread hostage forever. SO_RCVTIMEO or select() with timeout is the fix.

  ---
  11. Protocol Mismatch — HTTP Framing Not Handled

  The proxy does raw byte copying, but HTTP is a framed protocol with Content-Length and chunked transfer encoding. The load balancer will break keep-alive connections, corrupt chunked responses, and fail on multi-request
  connections. This is why your test sends HTTP but the proxy just buffers raw TCP.

  ---
  12. Dead Code — close(server_socket) is Unreachable

  File: load-balancer/LoadBalancer.cpp:86

  The while(true) loop never exits, so close(server_socket) at line 86 is dead code. The server socket fd leaks. There's also no SIGINT/SIGTERM handler for graceful shutdown.

  ---
  13. Backlog Too Small

  File: load-balancer/LoadBalancer.cpp:74

  listen(server_socket, 10) — a backlog of 10 is tiny for a load balancer. Should be SOMAXCONN.

  ---
  Code Quality Issues

  14. client.cpp Is Untracked by Git

  Git status shows ?? client.cpp — it's not committed. Either add it to the repo or delete it. Interviewers browsing your repo won't see it.

  ---
  15. client.cpp Connects to Port 6000, Not 8080

  File: client.cpp:7

  #define SERVERPORT 6000
  The load balancer runs on 8080. The client in the same repo connects to 6000. These should match.

  ---
  16. client.cpp Uses Deprecated gethostbyname()

  File: client.cpp:43

  gethostbyname() is deprecated since POSIX.1-2008. Should use getaddrinfo() which is IPv6-compatible and thread-safe.

  ---
  17. VS Code Silencing Compiler Errors

  File: .vscode/settings.json

  "C_Cpp.errorSquiggles": "disabled"
  Error squiggles are turned off. This hides real compiler errors in the IDE and is likely why some bugs went unnoticed. Delete this setting.

  ---
  18. Hardcoded Ports Everywhere — No Configuration

  Ports 8001, 8002, 8003, 8080 are hardcoded in main.cpp AND server.js. There's no config file, no environment variables, no CLI argument parsing. A real load balancer reads backends from a config file.

  ---
  19. No Algorithm Abstraction

  The README mentions future support for round-robin and IP-hash, but the code has no interface for swapping algorithms. A simple strategy pattern (abstract SelectBackend() method) would demonstrate good OOP design — relevant
  for a resume project.

  ---
  20. server.js Is Node.js, Not C++

  The backend servers are Express.js apps. This is fine for testing, but it obscures what the C++ load balancer is doing and makes setup harder. A simple C++ HTTP echo server would make the project fully self-contained.

  ---
  21. std=c++11 in 2024 — Should Use C++17

  File: load-balancer/Makefile

  C++17 has been standard for years. std::optional, std::string_view, if constexpr, structured bindings — these show modern C++ fluency. C++11 signals an outdated project.

  ---
  22. No Tests

  testing/package.json has "test": "echo \"Error: no test specified\"". The "test" is just a manual HTTP blaster. There are no unit tests for find_least_connection_index() logic, no integration tests with assertions, no CI/CD
  (no GitHub Actions).

  ---
  23. README Has Placeholder <repository-url>

  File: README.md

  git clone <repository-url>
  This was never filled in. Looks sloppy on a resume project.

  ---
  Summary Table

  ┌───────────────────┬───────┬────────────────────────────────────────────────────────────┐
  │     Severity      │ Count │                          Examples                          │
  ├───────────────────┼───────┼────────────────────────────────────────────────────────────┤
  │ Critical bug      │ 6     │ Race condition, counter leak, proxy deadlock, data race    │
  ├───────────────────┼───────┼────────────────────────────────────────────────────────────┤
  │ Major design flaw │ 7     │ No health check, thread explosion, no timeout, dead code   │
  ├───────────────────┼───────┼────────────────────────────────────────────────────────────┤
  │ Code quality      │ 10    │ Untracked file, deprecated API, hardcoded values, no tests │
  └───────────────────┴───────┴────────────────────────────────────────────────────────────┘

  ---
  Priority Fix Order

  1. Fix the TOCTOU race condition (atomic select + increment)
  2. Fix the proxy loop (bidirectional forwarding + error checking)
  3. Fix the active_connections decrement bug
  4. Add SO_REUSEADDR + memset the structs
  5. Add a thread pool (even a basic one with std::queue + condition variable)
  6. Fix or delete CMakeLists.txt
  7. Add health checks with a background thread
  8. Add SIGINT handler for graceful shutdown
  9. Upgrade to C++17, commit client.cpp, fix the port mismatch
  10. Add GitHub Actions CI

  Want me to start fixing these one by one?