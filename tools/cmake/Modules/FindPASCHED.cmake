find_path(PASCHED_INCLUDE_DIR pasched.hpp)
find_library(PASCHED_LIBRARY pasched)
set(PASCHED_FOUND "NO")

if(PASCHED_INCLUDE_DIR AND PASCHED_LIBRARY)
    set(PASCHED_FOUND "YES")
endif(PASCHED_INCLUDE_DIR AND PASCHED_LIBRARY)
