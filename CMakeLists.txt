cmake_minimum_required(VERSION 3.22.1)
project(KVRaftCpp)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE "Debug")
set(EXECUTABLE_OUTPUT_PATH bin)
set(LIBRARY_OUTPUT_PATH lib)
include_directories(src/common/include)
include_directories(src/fiber/include)
include_directories(src/rpc/include)
include_directories(example)
include_directories(src/raftCore/include)
include_directories(src/raftRpcPro/include)
include_directories(src/raftClerk/include)
include_directories(src/skipList/include)
link_directories(lib)
add_subdirectory(src)
add_subdirectory(example)
add_library(
    skip_list_on_raft STATIC
    ${src_rpc}
    ${src_fiber}
    ${rpc_example}
    ${raftsource}
    ${src_raftCore}
    ${src_raftRpcPro}
)
target_link_libraries(skip_list_on_raft muduo_net muduo_base pthread dl)
add_custom_target(format
        COMMAND bash format.sh
        WORKING_DIRECTORY .
        COMMAND echo "format done!"
)


