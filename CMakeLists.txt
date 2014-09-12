cmake_minimum_required(VERSION 2.8)
project(pymetis)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

find_package(PythonInterp)
find_package(PythonLibs ${PYTHON_VERSION_STRING})
find_package(NumPy)
find_package(Boost COMPONENTS python)
find_package(Metis)

include_directories(
    ${Boost_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${NUMPY_INCLUDE_DIRS}
    ${METIS_INCLUDES}
    )

set(BUILD_SHARED_LIBS ON)

add_library(pymetis pymetis.cpp)
set_target_properties(pymetis PROPERTIES PREFIX "")
target_link_Libraries(pymetis ${Boost_LIBRARIES} ${METIS_LIBRARY})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_pymetis
    ${CMAKE_CURRENT_BINARY_DIR}/test_pymetis
    COPYONLY
    )
