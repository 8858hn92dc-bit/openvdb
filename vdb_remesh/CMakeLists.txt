cmake_minimum_required(VERSION 3.15)
project(vdb_remesh CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------------------
# Find OpenVDB
# vcpkg:    -DCMAKE_TOOLCHAIN_FILE=.../vcpkg/scripts/buildsystems/vcpkg.cmake
# manual:   -DOpenVDB_ROOT=C:/path/to/openvdb
# -------------------------------------------------------------------
find_package(OpenVDB REQUIRED)

add_executable(vdb_remesh vdb_remesh.cpp)

target_link_libraries(vdb_remesh PRIVATE OpenVDB::openvdb)

# Windows: copy the required DLLs next to the exe after build
if(WIN32)
    add_custom_command(TARGET vdb_remesh POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:vdb_remesh>
            $<TARGET_FILE_DIR:vdb_remesh>
        COMMAND_EXPAND_LISTS
    )
endif()

install(TARGETS vdb_remesh RUNTIME DESTINATION bin)
