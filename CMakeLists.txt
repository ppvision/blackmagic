#此文件从模板自动生成! 请勿更改!
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
cmake_minimum_required(VERSION 3.28)

# specify cross-compilers and tools
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER  arm-none-eabi-gcc)
set(CMAKE_AR arm-none-eabi-ar)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP arm-none-eabi-objdump)
set(SIZE arm-none-eabi-size)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# project settings
project(MyBlackMagic C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

#target_compile_definitions(${TARGET}-tinyusb_config INTERFACE TU_ATTR_ALWAYS_INLINE=)

#Uncomment for hardware floating point
add_compile_definitions(ARM_MATH_CM4;ARM_MATH_MATRIX_CHECK;ARM_MATH_ROUNDING)
add_compile_options(-mfloat-abi=hard -mfpu=fpv4-sp-d16)
add_link_options(-mfloat-abi=hard -mfpu=fpv4-sp-d16)

#Uncomment for software floating point
#add_compile_options(-mfloat-abi=soft)

add_compile_options(-mcpu=cortex-m4 -mthumb -mthumb-interwork)
add_compile_options(-ffunction-sections -fdata-sections -fno-common -fmessage-length=0)

# uncomment to mitigate c++17 absolute addresses warnings
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-register")

# Enable assembler files preprocessing
add_compile_options($<$<COMPILE_LANGUAGE:ASM>:-x$<SEMICOLON>assembler-with-cpp>)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    message(STATUS "Maximum optimization for speed")
    add_compile_options(-Ofast)
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
    message(STATUS "Maximum optimization for speed, debug info included")
    add_compile_options(-Ofast -g)
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel")
    message(STATUS "Maximum optimization for size")
    add_compile_options(-Os)
else ()
    message(STATUS "Minimal optimization, debug info included")
    add_compile_options(-Og -g)
endif ()

include_directories(
        .
        config
        src/include
        src/target
        src/platforms/common/blackpill-f4
        src/platforms/blackpill-f411ce
        src/platforms/common
        src/platforms/common/stm32
        src/platforms/common/blackpill-
        deps/libopencm3/include
)

add_definitions(-DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -DSTM32F4 -DENABLE_RISCV)


set(BLACKMAGIC_SRC 
    src/command.c
    src/crc32.c
    src/exception.c
    src/gdb_main.c
    src/gdb_packet.c
    src/hex_utils.c
    src/main.c
    src/maths_utils.c
    src/morse.c
    src/remote.c
    src/timing.c
    src/platforms/common/jtagtap.c
    src/platforms/common/swdptap.c
    src/platforms/common/usb.c
    src/platforms/common/usb_dfu_stub.c
    src/platforms/common/aux_serial.c
    src/platforms/common/usb_serial.c
    src/platforms/common/stm32/gdb_if.c
    src/platforms/common/stm32/serialno.c
    src/platforms/common/stm32/timing_stm32.c
    src/platforms/common/stm32/traceswodecode.c
    src/platforms/common/stm32/traceswo.c
    src/platforms/common/blackpill-f4/blackpill-f4.c

    deps/libopencm3/lib/stm32/common/gpio_common_f0234.c
    deps/libopencm3/lib/stm32/common/gpio_common_all.c
    deps/libopencm3/lib/stm32/common/timer_common_f0234.c
    deps/libopencm3/lib/stm32/common/rcc_common_all.c
    deps/libopencm3/lib/stm32/common/timer_common_all.c
    deps/libopencm3/lib/stm32/common/spi_common_all.c
    deps/libopencm3/lib/stm32/common/spi_common_v1.c
    deps/libopencm3/lib/stm32/common/usart_common_all.c
    deps/libopencm3/lib/stm32/common/dma_common_f24.c
    deps/libopencm3/lib/stm32/common/flash_common_idcache.c
    deps/libopencm3/lib/stm32/common/flash_common_all.c
    deps/libopencm3/lib/stm32/common/usart_common_f124.c
    deps/libopencm3/lib/stm32/f4/rcc.c
    deps/libopencm3/lib/stm32/f4/pwr.c
    deps/libopencm3/lib/cm3/nvic.c
    deps/libopencm3/lib/cm3/scb.c
    deps/libopencm3/lib/cm3/systick.c
    deps/libopencm3/lib/cm3/vector.c
    deps/libopencm3/lib/usb/usb_f107.c
    deps/libopencm3/lib/usb/usb.c
    deps/libopencm3/lib/usb/usb_microsoft.c
    deps/libopencm3/lib/usb/usb_standard.c
    deps/libopencm3/lib/usb/usb_control.c
    deps/libopencm3/lib/usb/usb_dwc_common.c

) 


file( GLOB TARGET_LIST src/target/*.c )




set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/balckmagic-f411ce.ld)

add_link_options(-Wl,-gc-sections,--print-memory-usage,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map)
add_link_options(-mcpu=cortex-m4 -mthumb -mthumb-interwork --specs=nosys.specs -nostartfiles)
add_link_options(-T ${LINKER_SCRIPT})

add_executable(${PROJECT_NAME}.elf ${SOURCES} ${BLACKMAGIC_SRC} ${TARGET_LIST} ${LINKER_SCRIPT})


set(HEX_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.hex)
set(BIN_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.bin)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${HEX_FILE}
        COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${PROJECT_NAME}.elf> ${BIN_FILE}
        COMMENT "Building ${HEX_FILE}
Building ${BIN_FILE}")
