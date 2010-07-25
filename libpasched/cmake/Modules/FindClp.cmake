find_library(CLP_LIBRARY Clp)
set(CLP_FOUND "NO")
set(CLP_INCLUDE_DIR "")

if(CLP_LIBRARY)
    set(CLP_FOUND "YES")
endif(CLP_LIBRARY)
