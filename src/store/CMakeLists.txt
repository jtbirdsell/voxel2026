# Persistence store (issue #19, ADR-0013). SQLite amalgamation pinned by
# URL + SHA3-256 (sqlite.org publishes the hash); public domain, so no
# license election. SQLite symbols stay PRIVATE to this library — consumers
# see only the KvStore interface.

include(FetchContent) # C is enabled at the project() root for this dir

FetchContent_Declare(
	sqlite3
	URL https://sqlite.org/2026/sqlite-amalgamation-3530200.zip
	URL_HASH SHA3_256=81142986038e18f96c4a54e1a72562ae17e502a916f2a7701eff43388cbf1a40
)
FetchContent_MakeAvailable(sqlite3)

# Third-party target: compiled with its own (default) flags, never ours.
add_library(voxel2026_sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(voxel2026_sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})

add_library(voxel2026_store STATIC
	sqlite_store.cpp
)
target_include_directories(voxel2026_store PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(voxel2026_store
	PRIVATE voxel2026_warnings voxel2026_sqlite3
)
