add_library(openads_aes_impl OBJECT
    ${CMAKE_SOURCE_DIR}/third_party/tinyaes/aes128_impl.c
    ${CMAKE_SOURCE_DIR}/third_party/tinyaes/aes256_impl.c
)

target_include_directories(openads_aes_impl PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/tinyaes
)

if(MSVC)
    target_compile_options(openads_aes_impl PRIVATE /wd4244 /wd4267)
else()
    target_compile_options(openads_aes_impl PRIVATE
        -Wno-shadow -Wno-conversion -Wno-pedantic)
endif()

add_library(openads_core STATIC
    util/log.cpp
    platform/path.cpp
    platform/time.cpp
    platform/thread.cpp
    abi/ace_exports.cpp
    abi/ace_stubs.cpp
    abi/last_error.cpp
    abi/charset.cpp
    session/handle_registry.cpp
    session/connection.cpp
    engine/table.cpp
    engine/cursor.cpp
    engine/lock_mgr.cpp
    engine/order.cpp
    engine/index_expr.cpp
    engine/aes.cpp
    engine/tx.cpp
    engine/tx_log.cpp
    engine/lsn_map.cpp
    engine/data_dict.cpp
    engine/fts.cpp
    engine/codepage.cpp
    sql/parser.cpp
    drivers/dbf_common.cpp
    drivers/cdx/cdx_driver.cpp
    drivers/cdx/cdx_index.cpp
    drivers/ntx/ntx_driver.cpp
    drivers/ntx/ntx_index.cpp
    drivers/dbt/dbt_memo.cpp
    drivers/fpt/fpt_memo.cpp
    network/wire.cpp
    network/server.cpp
    network/client.cpp
)

if(WIN32)
    target_sources(openads_core PRIVATE
        platform/file_win32.cpp
        platform/lock_win32.cpp
        platform/mmap_win32.cpp
        platform/dll_win32.cpp
        network/socket_win32.cpp
    )
    target_link_libraries(openads_core PUBLIC ws2_32)
else()
    target_sources(openads_core PRIVATE
        platform/file_posix.cpp
        platform/lock_posix.cpp
        platform/mmap_posix.cpp
        platform/dll_posix.cpp
        network/socket_posix.cpp
    )
endif()

target_include_directories(openads_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party/tinyaes
)

target_link_libraries(openads_core PRIVATE openads_aes_impl)

if(UNIX)
    target_link_libraries(openads_core PUBLIC pthread)
endif()

if(OPENADS_WITH_TLS)
    target_sources(openads_core PRIVATE network/tls_transport.cpp)
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_TLS=1)
    target_link_libraries(openads_core PRIVATE
        MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto)
endif()

# ----------------------------------------------------------------------
# Drop-in replacement DLL: ace32.dll (x86) / ace64.dll (x64).
# Harbour's contrib/rddads links against ace32.lib / ace64.lib import
# libraries; placing this DLL on PATH replaces the SAP-shipped
# ace32.dll / ace64.dll without recompiling consuming applications.
# ----------------------------------------------------------------------
add_library(openads_ace SHARED
    abi/legacy_crt_shims.cpp
)

if(WIN32)
    # Default .def is name-based exports. Apps linked against an
    # ace32.lib / ace64.lib whose imports are by-ordinal need a
    # custom .def whose @N values match the SAP-shipped DLL the
    # user is replacing — generate it once per-machine with
    # tools/scripts/sap_ordinals_to_def.py and pass it in via
    # -DOPENADS_ACE_DEF=<path> at configure time.
    set(OPENADS_ACE_DEF "${CMAKE_CURRENT_SOURCE_DIR}/openads_ace.def"
        CACHE FILEPATH
        "Path to the .def file driving openads_ace exports.")
    target_sources(openads_ace PRIVATE ${OPENADS_ACE_DEF})
    message(STATUS "openads_ace exports def: ${OPENADS_ACE_DEF}")
endif()

target_include_directories(openads_ace
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/third_party/tinyaes
)

target_link_libraries(openads_ace PRIVATE openads_core)

# Output name follows ACE convention.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(openads_ace PROPERTIES OUTPUT_NAME "ace64")
else()
    set_target_properties(openads_ace PROPERTIES OUTPUT_NAME "ace32")
endif()
