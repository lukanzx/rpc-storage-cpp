get_filename_component(SRC_DIR "." ABSOLUTE)
aux_source_directory(${SRC_DIR} SRC_LIST)
set(src_raftCore ${SRC_LIST} CACHE INTERNAL "Description of the variable")




