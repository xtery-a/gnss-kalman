# ==============================================================================
# CMake Toolchain File for ARM Cortex-M33 (Bare-Metal Embedded Target)
# Target Architecture: ARMv8-M Mainline (STM32H5 / STM32U5 / NRF5340)
# ==============================================================================

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain prefix (GNU Arm Embedded Toolchain)
set(TOOLCHAIN_PREFIX arm-none-eabi- CACHE STRING "Cross-compilation toolchain prefix")

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}ar)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP      ${TOOLCHAIN_PREFIX}objdump)
set(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}size)

# Cortex-M33 Hardware Architecture Flags: Hard-float FPU & DSP
set(ARM_CORTEX_FLAGS "-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb")

# C Compilation Flags (Zero-Heap, Dead Code Elimination, Strict Warnings)
set(CMAKE_C_FLAGS_INIT   "${ARM_CORTEX_FLAGS} -std=c99 -Wall -Wextra -Werror -pedantic -ffunction-sections -fdata-sections -O2")
set(CMAKE_ASM_FLAGS_INIT "${ARM_CORTEX_FLAGS} -x assembler-with-cpp")

# Linker Flags (Section garbage collection, nano specs)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${ARM_CORTEX_FLAGS} -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs")

# Static library target for CMake try-compile during cross-compilation checks
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search root paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
