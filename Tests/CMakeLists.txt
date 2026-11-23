cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ECGBackendHostTests LANGUAGES C)
endif()

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(ECG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(CORE_INCLUDE_DIR "${ECG_ROOT}/Core/Inc")
set(CMSIS_DSP_INCLUDE_DIR "${ECG_ROOT}/Drivers/CMSIS/DSP/Include")
set(CMSIS_INCLUDE_DIR "${ECG_ROOT}/Drivers/CMSIS/Include")

add_executable(test_ecg_signal
    test_ecg_signal.c
    "${ECG_ROOT}/Core/Src/ecg_signal.c"
    "${ECG_ROOT}/Core/Src/IIR.c"
    "${ECG_ROOT}/Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c"
    "${ECG_ROOT}/Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c"
)
target_include_directories(test_ecg_signal PRIVATE
    "${CORE_INCLUDE_DIR}"
    "${CMSIS_DSP_INCLUDE_DIR}"
    "${CMSIS_INCLUDE_DIR}"
)
if(NOT MSVC)
    target_link_libraries(test_ecg_signal PRIVATE m)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_ecg_signal PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME ecg_signal COMMAND test_ecg_signal)
set_tests_properties(ecg_signal PROPERTIES LABELS "signal")

add_executable(test_ecg_stage3
    test_ecg_stage3.c
    "${ECG_ROOT}/Core/Src/ecg_rpeak.c"
    "${ECG_ROOT}/Core/Src/heart_rate.c"
    "${ECG_ROOT}/Core/Src/ecg_measure.c"
)
target_include_directories(test_ecg_stage3 PRIVATE
    "${CORE_INCLUDE_DIR}"
    "${CMSIS_DSP_INCLUDE_DIR}"
    "${CMSIS_INCLUDE_DIR}"
)
add_test(NAME ecg_rpeak COMMAND test_ecg_stage3 --module rpeak)
set_tests_properties(ecg_rpeak PROPERTIES LABELS "rpeak")
add_test(NAME ecg_heart_rate COMMAND test_ecg_stage3 --module heart_rate)
set_tests_properties(ecg_heart_rate PROPERTIES LABELS "heart_rate")
add_test(NAME ecg_measure COMMAND test_ecg_stage3 --module measure)
set_tests_properties(ecg_measure PROPERTIES LABELS "measure")
add_test(NAME ecg_pipeline COMMAND test_ecg_stage3 --module pipeline)
set_tests_properties(ecg_pipeline PROPERTIES LABELS "pipeline")

add_executable(test_ecg_stage4
    test_ecg_stage4.c
    "${ECG_ROOT}/Core/Src/key_input.c"
)
target_include_directories(test_ecg_stage4 PRIVATE
    "${CORE_INCLUDE_DIR}"
    "${CMSIS_DSP_INCLUDE_DIR}"
    "${CMSIS_INCLUDE_DIR}"
)
add_test(NAME ecg_stage4 COMMAND test_ecg_stage4)
set_tests_properties(ecg_stage4 PROPERTIES LABELS "key_input")
