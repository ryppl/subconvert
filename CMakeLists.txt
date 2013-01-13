cmake_minimum_required(VERSION 2.8)

list(APPEND CMAKE_MODULE_PATH 
  ${CMAKE_CURRENT_LIST_DIR}/lib/cmake-cxx11/Modules
  )
include(CheckCXX11Features)

add_subdirectory(lib/libgit2)

find_package(Boost COMPONENTS system filesystem REQUIRED)

include_directories(
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/lib/libgit2/include
  ${Boost_INCLUDE_DIRS}
  )

# Installation paths
SET(INSTALL_BIN bin CACHE PATH "Where to install binaries to.")
SET(INSTALL_LIB lib CACHE PATH "Where to install libraries to.")
SET(INSTALL_INC include CACHE PATH "Where to install headers to.")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  list(APPEND CMAKE_CXX_FLAGS "-std=c++11 -Werror")

  set_property(
    TARGET git2
    APPEND_STRING
    PROPERTY COMPILE_FLAGS "-Wno-unused-value -Wno-sizeof-pointer-memaccess"
    )
endif()

list(APPEND CMAKE_CXX_FLAGS ${CXX11_FEATURE_LIST})

add_library(gitutil STATIC
  src/gitutil.cpp)

add_executable(subconvert
  src/authors.cpp
  src/branches.cpp
  src/converter.cpp
  src/main.cpp
  src/svndump.cpp
)

add_executable(git-monitor
  src/git-monitor.cpp
)

target_link_libraries(gitutil 
  git2
  ${Boost_FILESYSTEM_LIBRARY}
  ${Boost_SYSTEM_LIBRARY}
)

target_link_libraries(subconvert gitutil)
target_link_libraries(git-monitor gitutil)

install(
  TARGETS subconvert git-monitor gitutil
  RUNTIME DESTINATION ${INSTALL_BIN}
  LIBRARY DESTINATION ${INSTALL_LIB}
  ARCHIVE DESTINATION ${INSTALL_LIB}
  )



