cmake_minimum_required(VERSION 3.16)
project(BlackScholes CXX)

#Use C++ 20 if possible — not strictly necessary but good practice for modern C++.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── compiler warnings ─────────────────────────────────────────────────────────
# COmpiler warnings are treated as errors. 

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Werror
)

# ── shared library ────────────────────────────────────────────────────────────
# Build BS core as a static lib so both targets link against the same object.
# More pricers will be added later, and this way we avoid code duplication.

add_library(bs_core STATIC
    functions/black_scholes.cpp
    functions/iv_solver.cpp
    functions/binomial_tree.cpp
)

target_include_directories(bs_core PUBLIC ${CMAKE_SOURCE_DIR})

# ── main executable ───────────────────────────────────────────────────────────

add_executable(pricer main.cpp)
target_link_libraries(pricer PRIVATE bs_core)

# ── test executable ───────────────────────────────────────────────────────────

add_executable(parity_test tests/parity_test.cpp)
target_link_libraries(parity_test PRIVATE bs_core)

# --─ vol surface executable ─────────────────────────────────────────────────────

add_executable(vol_surface functions/vol_surface.cpp)
target_link_libraries(vol_surface PRIVATE bs_core)

# ── CTest integration ─────────────────────────────────────────────────────────
# Lets you run `ctest` from the build dir. CI pipelines expect this.

enable_testing()
add_test(NAME PutCallParity COMMAND parity_test)