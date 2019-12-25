cmake_minimum_required(VERSION 3.6)
project(snippets)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wno-pointer-arith -Wno-unused-result")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -Wno-pointer-arith -Wno-unused-result")
if (APPLE)
    include_directories("/usr/local/include/")
    link_directories("/usr/local/lib/")
    link_directories("/usr/local/opt/openssl/lib/")
endif()
find_package(leveldb)
find_package(rocksdb)
find_package(curl)
find_package(uv)
find_package(pcre)
find_package(re2)
find_package(nghttp2)
find_package(openssl)
find_package(fmt)

add_executable(test_keepalive "test_keepalive.c")

add_executable(ldb "ldb.c")
target_link_libraries(ldb leveldb)

add_executable(multi_demo "multi_demo.c")
target_link_libraries(multi_demo curl)

add_executable(multi_get "multi_get.c")
target_link_libraries(multi_get curl)

add_executable(multi_uv "multi_uv.c")
target_link_libraries(multi_uv curl)
target_link_libraries(multi_uv uv)

add_executable(naive_curl "naive_curl.c")
target_link_libraries(naive_curl curl)

add_executable(pcre_demo "pcre_demo.c")
target_link_libraries(pcre_demo pcre)

add_executable(rdb "rdb.c")
target_link_libraries(rdb rocksdb)

if (UNIX AND NOT APPLE)
    add_executable(bench_clock "bench_clock.c")

    add_executable(sendfile "sendfile.c")
endif()

add_executable(sysinfo "sysinfo.c")
target_link_libraries(sysinfo pthread)

add_executable(uv_echo "uv_echo.c")
target_link_libraries(uv_echo uv)

add_executable(re2_demo "re2_demo.cpp")
target_link_libraries(re2_demo re2)

add_executable(string_printf "string_printf.cpp")

add_executable(convert_curl_to_ffmpeg "convert_curl_to_ffmpeg.c")

add_executable(id_gen "id_gen.cpp")
target_link_libraries(id_gen pthread)

add_executable(h2client "h2client.c")
target_link_libraries(h2client nghttp2)
target_link_libraries(h2client ssl)
target_link_libraries(h2client crypto)

add_executable(bench_fmt "bench_fmt.cpp")
target_link_libraries(bench_fmt fmt)

add_executable(chash_test "chash_test.cpp")

add_executable(cache_simulator "cache_simulator.cpp")

add_executable(fork_test "fork_test.c")
target_link_libraries(fork_test pthread)
