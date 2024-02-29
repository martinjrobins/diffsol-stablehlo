
cmake_minimum_required(VERSION 3.12)

# Definitions
set(PROJECT_NAME diffsol_stablehlo)
project(${PROJECT_NAME} VERSION 0.1.0 LANGUAGES C)

set( CMAKE_EXPORT_COMPILE_COMMANDS 1 )

# Set global compiler warnings
if(MSVC)
    add_compile_options(/W3 /WX)
else()
    add_compile_options(-Wall -pedantic)
endif()

set(LIBRARY_NAME ${PROJECT_NAME})
set(TEST_NAME ${PROJECT_NAME}_test)

set(TEST_FILES 
  tests/test_jit.c
)
add_definitions(-DUNITY_INCLUDE_DOUBLE)
set(SOURCE_FILES 
  src/jit.c
)
set(HEADER_FILES 
  src/jit.h
)

set(CMAKE_C_STANDARD 11)


## library
add_library(${LIBRARY_NAME} STATIC ${SOURCE_FILES} ${HEADER_FILES})
target_include_directories(${LIBRARY_NAME} PUBLIC src)


enable_testing()

# Unity tests
add_executable(${TEST_NAME} ${TEST_FILES})
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

# Add Sundials
include(FetchContent)
set(FETCHCONTENT_BASE_DIR ${PROJECT_BINARY_DIR} CACHE PATH "Directory to put third-party libs." FORCE)
FetchContent_Declare(
  iree
  GIT_REPOSITORY https://github.com/openxla/iree.git
  GIT_TAG        main
)

FetchContent_GetProperties(iree)
if(NOT iree_POPULATED)
  FetchContent_Populate(iree)
  add_subdirectory(${iree_SOURCE_DIR} ${iree_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

set(iree_TARGETS 
    iree_compiler_bindings_c_loader
)
target_link_libraries(${LIBRARY_NAME} PUBLIC ${iree_TARGETS})

# Add Unity 
FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG        v2.6.0_RC1
)

FetchContent_GetProperties(unity)
if(NOT unity_POPULATED)
  FetchContent_Populate(unity)
  add_subdirectory(${unity_SOURCE_DIR} ${unity_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

set(Unity_TARGETS unity)
target_link_libraries(${TEST_NAME} PRIVATE ${Unity_TARGETS} ${LIBRARY_NAME} ${Sundials_TARGETS})
target_include_directories(${TEST_NAME} PUBLIC "${FETCHCONTENT_BASE_DIR}/unity-src")


# Install library
set_property(TARGET ${LIBRARY_NAME} PROPERTY INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
install(TARGETS ${LIBRARY_NAME} DESTINATION lib)

# Install runtime
set_property(TARGET ${RUNTIME_NAME} PROPERTY INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
install(TARGETS ${RUNTIME_NAME} DESTINATION lib)

# Install library headers
install(FILES ${HEADER_FILES} DESTINATION include)
