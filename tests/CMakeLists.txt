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
    unit/engine_memo_test.cpp
    unit/abi_m4_smoke_test.cpp
    unit/abi_m5_smoke_test.cpp
    unit/tx_log_test.cpp
    unit/abi_m5x_recovery_test.cpp
    unit/abi_savepoint_test.cpp
    unit/data_dict_test.cpp
    unit/abi_dd_test.cpp
    unit/sql_parser_test.cpp
    unit/abi_sql_smoke_test.cpp
    unit/lsn_map_test.cpp
    unit/index_expr_test.cpp
    unit/abi_m92_real_test.cpp
    unit/abi_create_table_test.cpp
    unit/abi_refresh_extract_test.cpp
    unit/abi_create_index61_test.cpp
    unit/abi_zap_pack_test.cpp
    unit/abi_reindex_test.cpp
    unit/ntx_multilevel_test.cpp
    unit/abi_copy_table_test.cpp
    unit/abi_find_table_test.cpp
    unit/abi_binary_memo_test.cpp
    unit/abi_ntx_multitag_test.cpp
    unit/abi_server_info_test.cpp
    unit/abi_unicode_test.cpp
    unit/abi_lock_retry_test.cpp
    unit/abi_fts_test.cpp
    unit/abi_custom_key_test.cpp
    unit/abi_fts_search_test.cpp
    unit/abi_sql_contains_test.cpp
    unit/index_expr_utf8_test.cpp
    unit/abi_misc_impls_test.cpp
    unit/abi_mgmt_test.cpp
    unit/abi_dd_crud_test.cpp
)

target_include_directories(openads_unit_tests SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/doctest
)
target_include_directories(openads_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(openads_unit_tests PRIVATE openads_core)

add_test(NAME openads_unit_tests COMMAND openads_unit_tests)

# Harbour smoke fixture builder (M8.6). Produces a CDX matching the
# M8.5 fixture DBF using OpenADS' own CdxIndex::create + insert path,
# so the Harbour smoke tests dbSeek end-to-end through OpenADS.
add_executable(make_cdx harbour_smoke/make_cdx.cpp)
target_include_directories(make_cdx PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(make_cdx PRIVATE openads_core)

add_executable(probe_cdx harbour_smoke/probe_cdx.cpp)
target_include_directories(probe_cdx PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(probe_cdx PRIVATE openads_core)
