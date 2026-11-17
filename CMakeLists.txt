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

# ── market validation harness ─────────────────────────────────────────────────
# Runs the engine over live option chains (see README_VALIDATION.md).
# The harness includes "black_scholes.h" directly, so it needs include/ on the path.

add_executable(market_validation tests/market_validation.cpp)
target_link_libraries(market_validation PRIVATE bs_core)
target_include_directories(market_validation PRIVATE ${CMAKE_SOURCE_DIR}/include)
# The CSV loader indexes std::vector with a std::map<std::string, int> column
# map, which trips -Wsign-conversion (implied by -Wconversion on clang).
# Suppressed for this target only; the proper fix is map<std::string, size_t>
# in load(). All other warnings, and -Werror, stay on.
target_compile_options(market_validation PRIVATE -Wno-sign-conversion)

# ── CTest integration ─────────────────────────────────────────────────────────
# Lets you run `ctest` from the build dir. CI pipelines expect this.

enable_testing()
add_test(NAME PutCallParity COMMAND parity_test)