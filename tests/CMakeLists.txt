add_executable(openads_unit_tests
    unit/doctest_main.cpp
    unit/util_result_test.cpp
    unit/util_span_test.cpp
    unit/util_log_test.cpp
    unit/platform_file_test.cpp
    unit/platform_lock_test.cpp
    unit/platform_mmap_test.cpp
    unit/platform_path_test.cpp
    unit/platform_time_test.cpp
    unit/platform_thread_test.cpp
    unit/dbf_header_test.cpp
    unit/dbf_field_test.cpp
    unit/dbf_record_test.cpp
    unit/dbf_write_test.cpp
    unit/ntx_driver_test.cpp
    unit/lock_mgr_test.cpp
    unit/engine_table_test.cpp
    unit/engine_table_write_test.cpp
    unit/engine_cursor_test.cpp
    unit/session_handle_registry_test.cpp
    unit/session_connection_test.cpp
    unit/abi_smoke_test.cpp
    unit/abi_write_smoke_test.cpp
    unit/ntx_index_test.cpp
    unit/cdx_index_test.cpp
    unit/engine_order_test.cpp
    unit/engine_scope_test.cpp
    unit/abi_index_smoke_test.cpp
    unit/aes_test.cpp
    unit/dbt_memo_test.cpp
    unit/fpt_memo_test.cpp
)

target_include_directories(openads_unit_tests SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/doctest
)
target_include_directories(openads_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(openads_unit_tests PRIVATE openads_core)

add_test(NAME openads_unit_tests COMMAND openads_unit_tests)
