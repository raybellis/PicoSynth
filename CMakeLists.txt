cmake_minimum_required(VERSION 3.30)
set(PROJECT PicoSynth)

#----------------------------------------------------------------------
#
# system configuration
#
#----------------------------------------------------------------------

# target device.  a build directory bakes these into its cache and
# can't be moved between them, so switching target means configuring
# a new one rather than re-running cmake over the old
set(CONFIG_PICO_PLATFORM rp2350)
set(CONFIG_PICO_BOARD    pico2)

# audio parameters.
#
# 288 samples at 48 kHz is 6 ms, which is exactly 6 USB frames - the
# block size and the USB frame rate line up, which the USB audio
# backend wants.  it is also the size that keeps the *envelopes* close
# to how they were tuned: they tick once per buffer with fixed
# increments, so their timing follows the buffer period, and 6 ms is
# near the 5.805 ms of the old 44.1 kHz / 256 arrangement.
#
# the LFO moves the other way - its rate is note_table[f] / buffer
# size, so a bigger block makes it slower, 11% here.  no block size
# preserves both; envelopes won because they are audible on every
# note, where the LFO only speaks when the mod wheel is up
set(CONFIG_SAMPLE_RATE 48000)
set(CONFIG_WAVE_SHIFT  11)
set(CONFIG_BUFFER_SIZE 288)

# audio transport: 0 for I2S, 1 for USB audio (a UAC2 input device,
# alongside the existing MIDI interface)
set(CONFIG_AUDIO_USB 0)

# set to 1 to enable LCD debug output
set(CONFIG_LCD_ACTIVE 1)

# select I2S audio option
set(CONFIG_HW_PIMORONI_AUDIO 1)
set(CONFIG_HW_PICOADK 0)

#----------------------------------------------------------------------

if (${CONFIG_HW_PIMORONI_AUDIO})
	set(CONFIG_I2S_DATA_PIN 9)
	set(CONFIG_I2S_CLOCK_PIN_BASE 10)
elseif (${CONFIG_PICOADK})
	set(CONFIG_I2S_DATA_PIN 16)
	set(CONFIG_I2S_CLOCK_PIN_BASE 17)
else()
	set(CONFIG_I2S_DATA_PIN 28)
	set(CONFIG_I2S_CLOCK_PIN_BASE 26)
endif()

#----------------------------------------------------------------------

# import required SDKs.  the platform has to be settled before the SDK
# is imported, since that is what picks the compiler flags

set(PICO_PLATFORM ${CONFIG_PICO_PLATFORM})
set(PICO_BOARD ${CONFIG_PICO_BOARD})

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)
if (${CONFIG_LCD_ACTIVE})
	include($ENV{PICO_HOME}/pimoroni-pico/pimoroni_pico_import.cmake)
endif()

# write a compile_commands.json out into the build tree.  an editor
# has no other way to find either the SDK's headers or the generated
# ones, since neither is anywhere near the source
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set up project and SDK
project(${PROJECT} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# version.h is the only generated file left; the lookup tables are now
# computed at startup by src/tables.c and src/waves.c, which is what
# retired the NodeJS build dependency

set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/tables)

# the git revision, regenerated on every build rather than at configure
# time - it exists to say what is on the board, so it has to track
# commits.  the script only rewrites version.h when the revision
# actually changes, so this does not force a relink each time
if (${CONFIG_AUDIO_USB})
	set(AUDIO_BACKEND src/audio_usb.c)
else()
	set(AUDIO_BACKEND src/audio_i2s.c)
endif()

add_custom_target(version ALL
	COMMAND	${CMAKE_COMMAND}
			-D SRC=${CMAKE_CURRENT_LIST_DIR}
			-D OUT=${GENERATED}/version.h
			-P ${CMAKE_CURRENT_LIST_DIR}/utils/version.cmake
	BYPRODUCTS ${GENERATED}/version.h
	COMMENT	"Checking git revision"
	VERBATIM
)

add_executable(${PROJECT}
	src/main.cxx
	src/usb_descriptors.c
	${AUDIO_BACKEND}
	src/engine.cxx
	src/voice.cxx
	src/channel.cxx
	src/envelope.cxx
	src/filter.cxx
	src/presets.c
	src/tables.c
	src/waves.c
)

add_dependencies(${PROJECT} version)

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
target_include_directories(${PROJECT} PRIVATE ${GENERATED})

target_compile_options(${PROJECT} PRIVATE -Wall -Werror -O3)

target_compile_definitions(${PROJECT} PRIVATE
	PICO_AUDIO_I2S_MONO_INPUT=0
	PICO_AUDIO_I2S_MONO_OUTPUT=0
	PICO_AUDIO_I2S_DATA_PIN=${CONFIG_I2S_DATA_PIN}
	PICO_AUDIO_I2S_CLOCK_PIN_BASE=${CONFIG_I2S_CLOCK_PIN_BASE}
	CONFIG_SAMPLE_RATE=${CONFIG_SAMPLE_RATE}
	CONFIG_BUFFER_SIZE=${CONFIG_BUFFER_SIZE}
	CONFIG_WAVE_SHIFT=${CONFIG_WAVE_SHIFT}
	CONFIG_AUDIO_USB=${CONFIG_AUDIO_USB}
	CONFIG_LCD_ACTIVE=${CONFIG_LCD_ACTIVE}
	CONFIG_HW_PICOADK=${CONFIG_HW_PICOADK}
	CONFIG_HW_PIMORONI_AUDIO=${CONFIG_HW_PIMORONI_AUDIO}
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
