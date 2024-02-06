add_library(csdr-sstv SHARED csdr-sstv.cpp version.cpp)
file(GLOB LIBCSDRSSTV_HEADERS
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
)
set_target_properties(csdr-sstv PROPERTIES PUBLIC_HEADER "${LIBCSDRSSTV_HEADERS}")
target_link_libraries(csdr-sstv Csdr::csdr)
set_target_properties(csdr-sstv PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
install(TARGETS csdr-sstv
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/csdr-sstv
)