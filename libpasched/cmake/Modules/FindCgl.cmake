find_library(CGL_LIBRARY Cgl)
set(CGL_FOUND "NO")
set(CGL_INCLUDE_DIR "")

if(CGL_LIBRARY)
    set(CGL_FOUND "YES")
endif(CGL_LIBRARY)
