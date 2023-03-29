cmake_minimum_required(VERSION 3.13)
set(PROJECT PicoSynth)

include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)
include(pimoroni_pico_import.cmake)

project(${PROJECT} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(${PROJECT}
	src/main.cxx
	src/usb_descriptors.c
	src/audio.c
	src/engine.cxx
	src/channel.cxx
	src/patch.cxx
	src/oscillator.cxx
	src/envelope.cxx
)

pico_enable_stdio_usb(${PROJECT} 0)
pico_enable_stdio_uart(${PROJECT} 1)

target_include_directories(${PROJECT} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(${PROJECT} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(${PROJECT} PRIVATE -Wall -Werror -O3)

target_compile_definitions(${PROJECT} PRIVATE
	PICO_AUDIO_I2S_MONO_INPUT=0
	PICO_AUDIO_I2S_MONO_OUTPUT=0
	SAMPLE_CHANS=2
	PICO_AUDIO_I2S_DATA_PIN=26
	PICO_AUDIO_I2S_CLOCK_PIN_BASE=27
)

include(${PIMORONI_PICO_PATH}/libraries/pico_rgb_keypad/pico_rgb_keypad.cmake)

target_link_libraries(${PROJECT} PRIVATE
	pico_stdlib
	pico_audio_i2s
	hardware_interp
	pico_rgb_keypad
	tinyusb_device
	tinyusb_board
)

pico_add_extra_outputs(${PROJECT})
