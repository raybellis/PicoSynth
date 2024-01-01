cmake_minimum_required(VERSION 3.19)
set(PROJECT PicoSynth)

# set to 0 to disable LCD usage
set(CONFIG_LCD_ACTIVE 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)
if (${CONFIG_LCD_ACTIVE})
	include($ENV{PICO_HOME}/pimoroni-pico/pimoroni_pico_import.cmake)
endif()

project(${PROJECT} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

message("-- Building data files")

set(CONFIG_SAMPLE_RATE 44100)
set(CONFIG_WAVE_SHIFT  11)
set(CONFIG_BUFFER_SIZE 256)

execute_process(
	COMMAND ./utils/data.js ${CONFIG_SAMPLE_RATE} ${CONFIG_WAVE_SHIFT} ${CONFIG_BUFFER_SIZE}
	WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
)

execute_process(
	COMMAND ./utils/waves.js
	WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
)

add_executable(${PROJECT}
	src/main.cxx
	src/usb_descriptors.c
	src/audio.c
	src/engine.cxx
	src/channel.cxx
	src/envelope.cxx
	src/presets.c
	src/data.c
	src/waves.c
)

pico_enable_stdio_usb(${PROJECT} 0)
pico_enable_stdio_uart(${PROJECT} 1)

if (${CONFIG_LCD_ACTIVE})
include(common/pimoroni_bus)
include(libraries/pico_display_2/pico_display_2)
include(libraries/pico_graphics/pico_graphics)
include(drivers/st7789/st7789)
endif()

target_include_directories(${PROJECT} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(${PROJECT} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(${PROJECT} PRIVATE -Wall -Werror -O3)

target_compile_definitions(${PROJECT} PRIVATE
	PICO_AUDIO_I2S_MONO_INPUT=0
	PICO_AUDIO_I2S_MONO_OUTPUT=0
	PICO_AUDIO_I2S_DATA_PIN=9
	PICO_AUDIO_I2S_CLOCK_PIN_BASE=10
	CONFIG_LCD_ACTIVE=${CONFIG_LCD_ACTIVE}
)

target_link_libraries(${PROJECT} PRIVATE
	pico_stdlib
	pico_multicore
	pico_audio_i2s
	pico_unique_id
	hardware_interp
	hardware_uart
	hardware_irq
	tinyusb_device
	tinyusb_board
)

if (${CONFIG_LCD_ACTIVE})
target_link_libraries(${PROJECT} PRIVATE
	pico_display_2
	hardware_spi
	hardware_pwm
	pico_graphics
	st7789
)
endif()

pico_add_extra_outputs(${PROJECT})
