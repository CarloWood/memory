cmake_minimum_required(VERSION 3.12...4.0.3)

project(memory
  DESCRIPTION "C++ Memory utilities"
  LANGUAGES CXX
)

include(AICxxProject)

#==============================================================================
# POLICIES
#
if (POLICY CMP0167)
  # Use boost provided BoostConfig.cmake (requires boost version 1.70 or higher).
  # This policy was introduced in cmake 3.30.
  cmake_policy(SET CMP0167 NEW)
endif ()

#==============================================================================
# DEPENDENCIES
#

#==============================================================================
# OPTIONS

#==============================================================================
# PLATFORM SPECIFIC CHECKS
#

#==============================================================================
# BUILD OBJECT LIBRARY
#

# This project is an OBJECT-library, used by other git submodules and the main project.
add_library(memory_ObjLib OBJECT)

# The list of source files.
target_sources(memory_ObjLib
  PRIVATE
    "DequeMemoryResource.cxx"
    "MemoryPagePool.cxx"
    "MemoryMappedPool.cxx"
    "NodeMemoryPool.cxx"
    "SimpleSegregatedStorage.cxx"

    "DequeAllocator.h"
    "DequeMemoryResource.h"
    "MemoryPagePool.h"
    "MemoryMappedPool.h"
    "NodeMemoryPool.h"
    "NodeMemoryResource.h"
    "SimpleSegregatedStorage.h"
)

# Required include search-paths.
get_target_property(CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(memory_ObjLib
  PUBLIC
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}" # For sys.h and debug.h.
)

# Require support for C++20.
target_compile_features(memory_ObjLib
  PUBLIC cxx_std_20
)

# Set link dependencies.
target_link_libraries(memory_ObjLib
  PUBLIC
    AICxx::utils
    enchantum::enchantum
)

# Create an ALIAS target.
add_library(AICxx::memory ALIAS memory_ObjLib)

# Prepend this object library to the list.
set(AICXX_OBJECTS_LIST AICxx::memory ${AICXX_OBJECTS_LIST} CACHE INTERNAL "List of OBJECT libaries that this project uses.")
