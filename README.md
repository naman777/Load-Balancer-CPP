# Simple Load Balancer in C++

A simple, efficient load balancer implemented in C++ using an object-oriented approach. It uses the **least-connections strategy** to distribute incoming client connections across backend servers. The project supports multithreading and ensures a fair distribution of workload.

---

## 📂 LoadBalancer Structure (/load-balancer)

```plaintext
.
├── LoadBalancer.h       # Header file for the LoadBalancer class
├── LoadBalancer.cpp     # Implementation of the LoadBalancer class
├── main.cpp             # Main entry point of the application
├── Makefile             # Makefile for building the project (optional)
└── README.md            # Project documentation
```

---

## ✨ Features

- **Least-connections and Round-Robin** algorithms, selectable at startup.
- **Thread pool** (fixed-size, configurable) — no unbounded thread creation.
- **Bidirectional proxy forwarding** via `select()` with a 30-second idle timeout.
- **Health checks** — background thread probes each backend every 5 seconds and stops routing to downed servers.
- **Atomic backend selection** — algorithm selection and counter increment happen under a single lock, eliminating TOCTOU races.
- **Graceful shutdown** on SIGINT/SIGTERM — drains the accept loop and joins worker threads.
- `SO_REUSEADDR` enabled — load balancer can restart immediately after a crash without waiting for TCP TIME_WAIT.
- Clean, **object-oriented design** for easy understanding and maintenance.

---

## 🚀 Getting Started

### Prerequisites

- A **C++ compiler** (e.g., `g++`).
- Basic understanding of **C++ socket programming**.
- A **POSIX-compliant operating system** (Linux, macOS).

### 1️⃣ Clone the Repository

```bash
git clone https://github.com/naman777/Load-Balancer-CPP.git
cd Load-Balancer-CPP
cd load-balancer
```

### 2️⃣ Build the Project

#### Using `make`
Ensure you have a `Makefile` in the project directory. Run:

```bash
make
```

#### Without `make`
Manually compile the project using `g++`:

```bash
g++ -std=c++17 -pthread -c LoadBalancer.cpp main.cpp
g++ -std=c++17 -pthread -o load_balancer main.o LoadBalancer.o
```

### 3️⃣ Run the Load Balancer

```bash
./load_balancer
```

### 4️⃣ Test the Load Balancer

1. Start the server:

   ```bash
   cd ..
   cd server
   npm install
   node server.js
   ```

2. Start the testing server:

   ```bash
   cd ..
   cd testing
   npm install
   node index.js
   ```

---

## ⚙️ Configuration

- **Listening Port**: The load balancer listens on port `8080` by default.
- **Backend Ports**: Backend servers are defined as `{8001, 8002, 8003}` in the `main.cpp` file. You can modify these values if needed.

---

## 📄 Project Files

| File               | Description                                     |
|--------------------|-------------------------------------------------|
| `LoadBalancer.h`   | Declaration of the `LoadBalancer` class.        |
| `LoadBalancer.cpp` | Implementation of the load balancing logic.     |
| `main.cpp`         | Entry point of the application.                 |
| `Makefile`         | Optional: Automates the build process.          |
| `README.md`        | Project documentation.                          |

---

## 🛠️ Build & Test Example

Here’s an example of running the build and test:

```bash
# Build
make

# Run the load balancer
./load_balancer
```

For testing:

```bash
# Run the server
cd server
npm install
node server.js

# Run the testing server
cd testing
npm install
node index.js
```

---

## 📝 License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for more details.

---

## 🤝 Contributing

Contributions are welcome! Feel free to fork the repository, submit issues, or create pull requests.

---

## 🌟 Future Work

- Add IP-hash algorithm for sticky sessions.
- Support reading backend list and config from a file instead of hardcoded values.
- Replace the Node.js backend stubs with a self-contained C++ echo server.
- Add unit tests for `select_and_reserve_backend()` logic and integration tests with CI assertions.

---

