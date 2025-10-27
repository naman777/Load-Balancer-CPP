# Simple Load Balancer in C++

A simple, efficient load balancer implemented in C++ using an object-oriented approach. It uses the **least-connections strategy** to distribute incoming client connections across backend servers. The project supports multithreading and ensures a fair distribution of workload.

---

## 📂 LoadBalancer Structure (/load_balancer)

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

- **Least-connections strategy** for fair load distribution.
- **Multithreaded handling** of client connections for better performance.
- Clean, **object-oriented design** for easy understanding and maintenance.

---

## 🚀 Getting Started

### Prerequisites

- A **C++ compiler** (e.g., `g++`).
- Basic understanding of **C++ socket programming**.
- A **POSIX-compliant operating system** (Linux, macOS).

### 1️⃣ Clone the Repository

```bash
git clone <repository-url>
cd load_balancer
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
g++ -std=c++11 -pthread -c LoadBalancer.cpp main.cpp
g++ -std=c++11 -pthread -o load_balancer main.o LoadBalancer.o
```

### 3️⃣ Run the Load Balancer

```bash
./load_balancer
```

### 4️⃣ Test the Load Balancer

1. Start the server:

   ```bash
   cd server
   npm install
   node server.js
   ```

2. Start the testing server:

   ```bash
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

- Add support for more sophisticated load-balancing algorithms (e.g., round-robin, IP-hash).
- Implement health checks for backend servers.
- Add dynamic scaling of backend servers.

---

