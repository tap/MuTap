add_library(mutap_capi SHARED mutap_capi.cpp)
target_link_libraries(mutap_capi PRIVATE MuTap::MuTap mutap_warnings)
set_target_properties(mutap_capi PROPERTIES POSITION_INDEPENDENT_CODE ON)
