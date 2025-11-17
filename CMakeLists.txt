cmake_minimum_required(VERSION 3.10)
project(mqtt VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Project options
option(MQTT_BUILD_TESTS "Build MQTT tests" ON)
option(MQTT_BUILD_EXAMPLES "Build MQTT examples" OFF)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Library source files
set(MQTT_SOURCES
    src/mqtt.c
    src/mqtt_packet.c
    src/mqtt_helper.c
)

# Create library
add_library(mqtt ${MQTT_SOURCES})

# Set library properties
set_target_properties(mqtt PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# Install targets
install(TARGETS mqtt
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Tests
if(MQTT_BUILD_TESTS)
    enable_testing()
    
    add_executable(test_mqtt test/test_mqtt.c)
    target_link_libraries(test_mqtt mqtt)
    
    add_test(NAME test_mqtt COMMAND test_mqtt)
endif()

# Command line tool
add_executable(mqtt_client cmd/main.c cmd/trans.c)
target_link_libraries(mqtt_client mqtt)

# Examples
if(MQTT_BUILD_EXAMPLES)
    # Add examples here when needed
endif()