cmake_minimum_required(VERSION 3.20)

project(control_chassis LANGUAGES C ASM)

option(MOWER_BLADE_OK "Build mower OK blade control release candidate" OFF)
if(MOWER_BLADE_OK AND (TICKET03_EXP3_HIL OR TICKET03_EXP3_PWM_HIL))
    message(FATAL_ERROR "MOWER_BLADE_OK is separate from bench profiles")
endif()

option(TICKET03_EXP3_HIL "Build isolated Ticket03 Experiment3 transport" OFF)
option(TICKET03_EXP3_PWM_HIL "Enable Ticket03 CH4/OK PWM bench adapter" OFF)
if(TICKET03_EXP3_PWM_HIL AND NOT TICKET03_EXP3_HIL)
    message(FATAL_ERROR "TICKET03_EXP3_PWM_HIL requires TICKET03_EXP3_HIL")
endif()

if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi-gcc")
    message(FATAL_ERROR "This firmware requires Arm GNU Toolchain; configure with a CMake preset")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CORE_SOURCES
    Core/Src/adc.c
    Core/Src/can.c
    Core/Src/dma.c
    Core/Src/flash_storage.c
    Core/Src/freertos.c
    Core/Src/gpio.c
    Core/Src/main.c
    Core/Src/spi.c
    Core/Src/stm32f4xx_hal_msp.c
    Core/Src/stm32f4xx_hal_timebase_tim.c
    Core/Src/stm32f4xx_it.c
    Core/Src/system_stm32f4xx.c
    Core/Src/tim.c
    Core/Src/usart.c
)

set(USER_APP_SOURCES
    Core/UserApp/app_BMS_Sensor.c
    Core/UserApp/app_Comm.c
    Core/UserApp/app_Motion.c
    Core/UserApp/app_imu.c
    Core/UserApp/app_Report.c
    Core/UserApp/app_SerialTX.c
    Core/UserApp/app_context.c
    Core/UserApp/blade_manual_control.c
    Core/UserApp/safety_ownership_gate.c
    Core/UserApp/telemetry_snapshot.c
    Core/UserApp/battery_monitor.c
)

set(USER_DEVICE_SOURCES
    Core/UserDev/A28.c
    Core/UserDev/AXEBMS_RS485.c
    Core/UserDev/BladeMotor.c
    Core/UserDev/E34.C
    Core/UserDev/EG2104.c
    Core/UserDev/blade_confirm_ch6_adapter.c
    Core/UserDev/blade_hil_uart_debug.c
    Core/UserDev/bsp_imu_solver.c
    Core/UserDev/buzzer.c
    Core/UserDev/car_Control.c
    Core/UserDev/can_tx_scheduler.c
    Core/UserDev/can_tx_port.c
    Core/UserDev/chassis_control.c
    Core/UserDev/connet_check.c
    Core/UserDev/cruise_control.c
    Core/UserDev/flipsky_can.c
    Core/UserDev/flysky_ibus_servo.c
    Core/UserDev/heartbeat.c
    Core/UserDev/icm42688.c
    Core/UserDev/lifter.c
    Core/UserDev/grass_bag_control.c
    Core/UserDev/modbus_rtu_bus.c
    Core/UserDev/mssd_can.c
    Core/UserDev/odrive_can.c
    Core/UserDev/oid_can.c
    Core/UserDev/pairing_receiver.c
    Core/UserDev/read_adc.c
    Core/UserDev/remote_authorization.c
    Core/UserDev/remote_backend.c
    Core/UserDev/rs485.c
    Core/UserDev/sens_ibus_responder.c
    Core/UserDev/sens_ibus_usart2.c
    Core/UserDev/serial_port.c
    Core/UserDev/sr04_uart.c
    Core/UserDev/tricolor_led.c
)

set(HAL_SOURCES
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_can.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_adc.c
)

set(FREERTOS_SOURCES
    Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c
    Middlewares/Third_Party/FreeRTOS/Source/croutine.c
    Middlewares/Third_Party/FreeRTOS/Source/event_groups.c
    Middlewares/Third_Party/FreeRTOS/Source/list.c
    Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
    Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
    Middlewares/Third_Party/FreeRTOS/Source/queue.c
    Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c
    Middlewares/Third_Party/FreeRTOS/Source/tasks.c
    Middlewares/Third_Party/FreeRTOS/Source/timers.c
)

add_executable(${PROJECT_NAME}
    ${CORE_SOURCES}
    ${USER_APP_SOURCES}
    ${USER_DEVICE_SOURCES}
    ${HAL_SOURCES}
    ${FREERTOS_SOURCES}
    startup_stm32f407xx.s
)

if(TICKET03_EXP3_HIL OR MOWER_BLADE_OK)
    set(TICKET03_SOURCES
        ../experiments/ticket03/exp3.c
        ../experiments/ticket03/exp3_link.c
        ../experiments/ticket03/exp3_hil.c
        ../experiments/ticket03/stm32_adapter.c
    )
    if(TICKET03_EXP3_PWM_HIL OR MOWER_BLADE_OK)
        list(APPEND TICKET03_SOURCES
            ../experiments/ticket03/exp3_probe.c
            ../experiments/ticket03/exp3_blade_adapter.c
            ../experiments/ticket03/exp3_auto_session.c
        )
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            EXP3_PWM_PROBE_ENABLED=1 EXP3_AUTO_SESSION_ENABLED=1)
    endif()
    if(MOWER_BLADE_OK)
        target_compile_definitions(${PROJECT_NAME} PRIVATE MOWER_BLADE_OK_ENABLED=1 MOWER_GRASS_BAG_CH6_ENABLED=1)
    endif()
    target_sources(${PROJECT_NAME} PRIVATE ${TICKET03_SOURCES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE EXP3_ENABLED=1 EXP3_LOG_COUNT=64)
endif()

# The historical upper-case .C extension is C++ by convention; this source is C.
set_source_files_properties(Core/UserDev/E34.C PROPERTIES LANGUAGE C)

target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
    Middlewares/Third_Party/FreeRTOS/Source/include
    Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32F407xx
    REMOTE_BACKEND_A28_E34=0
    REMOTE_BACKEND_FLYSKY_IBUS=1
    BLADE_HIL_UART_DEBUG_ENABLED=0
    SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1
    SENS_IBUS_INTERNAL_PULLUP_EXPERIMENT=0
    SENS_IBUS_HIL_SCOPE_MARKER_ENABLED=1
    SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH=0
    SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED=1
)

set(MCU_FLAGS
    -mcpu=cortex-m4
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -mthumb
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${MCU_FLAGS}
    $<$<COMPILE_LANGUAGE:C>:-Wall;-ffunction-sections;-fdata-sections;-funsigned-char>
    $<$<CONFIG:Debug>:-Og;-g3>
    $<$<CONFIG:Release>:-Os;-g3>
)

target_link_options(${PROJECT_NAME} PRIVATE
    ${MCU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F407XX_FLASH.ld
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
)
target_link_libraries(${PROJECT_NAME} PRIVATE m)

set_target_properties(${PROJECT_NAME} PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    OUTPUT_NAME ${PROJECT_NAME}
    SUFFIX .elf
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Generating HEX/BIN images and reporting firmware size"
)

set(OPENOCD_INTERFACE "interface/stlink.cfg" CACHE STRING "OpenOCD debug adapter config")
set(OPENOCD_TARGET "target/stm32f4x.cfg" CACHE STRING "OpenOCD MCU target config")
set(STLINK_SERIAL "" CACHE STRING "Optional ST-Link serial number")
find_program(OPENOCD_EXECUTABLE NAMES openocd)

if(OPENOCD_EXECUTABLE)
    set(OPENOCD_SERIAL_ARGS)
    if(STLINK_SERIAL)
        list(APPEND OPENOCD_SERIAL_ARGS -c "adapter serial ${STLINK_SERIAL}")
    endif()

    add_custom_target(flash
        COMMAND ${OPENOCD_EXECUTABLE}
                -f ${OPENOCD_INTERFACE}
                -f ${OPENOCD_TARGET}
                ${OPENOCD_SERIAL_ARGS}
                -c "program $<TARGET_FILE:${PROJECT_NAME}> verify reset exit"
        DEPENDS ${PROJECT_NAME}
        USES_TERMINAL
        COMMENT "Flashing ${PROJECT_NAME}.elf with ST-Link/OpenOCD"
    )
else()
    message(WARNING "OpenOCD not found: firmware builds normally, but the 'flash' target is unavailable")
endif()
