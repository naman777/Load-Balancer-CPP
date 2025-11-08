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
