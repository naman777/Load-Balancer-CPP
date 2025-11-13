cmake_minimum_required(VERSION 3.14)
project(load_balancer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(load_balancer
    main.cpp
    LoadBalancer.cpp
)

target_compile_options(load_balancer PRIVATE -Wall -Wextra -O2)
target_link_libraries(load_balancer PRIVATE pthread)

# Echo server (C++ replacement for server.js)
add_executable(echo_server
    ../server/server.cpp
)
target_compile_options(echo_server PRIVATE -Wall -Wextra -O2)

# Unit tests
enable_testing()

add_executable(test_lb
    ../tests/test_lb.cpp
    LoadBalancer.cpp
)
target_compile_options(test_lb PRIVATE -Wall -Wextra -O2)
target_link_libraries(test_lb PRIVATE pthread)
add_test(NAME lb_tests COMMAND test_lb)
