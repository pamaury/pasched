find_library(COIN_UTILS_LIBRARY CoinUtils)
set(COIN_UTILS_FOUND "NO")
set(COIN_UTILS_INCLUDE_DIR "")

if(COIN_UTILS_LIBRARY)
    set(COIN_UTILS_FOUND "YES")
endif(COIN_UTILS_LIBRARY)
