cmake_minimum_required(VERSION 3.22)

option(ECG_HOST_VALIDATION "Build host-side ECG signal validation tests" OFF)
option(ECG_STAGE2_HIL "Enable stage 2 hardware-in-the-loop capture hooks" OFF)
option(ECG_STAGE2_HIL_INJECT_FILTER_ERROR
       "Force one filter error for the stage 2 HIL fault-screen test" OFF)

if(ECG_HOST_VALIDATION)
    project(ECG-backend-host-validation LANGUAGES C)

    enable_testing()

    add_executable(ecg_stage2_validation
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_ecg_signal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ecg_signal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/IIR.c
        ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
    )

    target_include_directories(ecg_stage2_validation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
        ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/DSP/Include
        ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include
    )

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ecg_stage2_validation PRIVATE -Wall -Wextra -Werror)
    endif()

    if(NOT MSVC)
        target_link_libraries(ecg_stage2_validation PRIVATE m)
    endif()

    add_test(NAME ecg_stage2_validation COMMAND ecg_stage2_validation)
    return()
endif()

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME ECG-backend)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

if(ECG_STAGE2_HIL)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ecg_stage2_hil.c
    )
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ECG_STAGE2_HIL=1)
endif()

if(ECG_STAGE2_HIL_INJECT_FILTER_ERROR)
    if(NOT ECG_STAGE2_HIL)
        message(FATAL_ERROR "ECG_STAGE2_HIL_INJECT_FILTER_ERROR requires ECG_STAGE2_HIL=ON")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        ECG_STAGE2_HIL_INJECT_FILTER_ERROR=1
    )
endif()

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/DELAY/delay.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/LCD/lcd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/LCD/lcd_init.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/IIR.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ecg_acquisition.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ecg_signal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ecg_rpeak.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/heart_rate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ecg_measure.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/key_input.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ecg_display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/DELAY
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/LCD
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/DSP/Include
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
