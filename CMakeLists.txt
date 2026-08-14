cmake_minimum_required(VERSION 3.19)
set(PROJECT PicoSynth)

#----------------------------------------------------------------------
#
# system configuration
#
#----------------------------------------------------------------------

# audio parameters
set(CONFIG_SAMPLE_RATE 44100)
set(CONFIG_WAVE_SHIFT  11)
set(CONFIG_BUFFER_SIZE 256)

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

# import required SDKs

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)
if (${CONFIG_LCD_ACTIVE})
	include($ENV{PICO_HOME}/pimoroni-pico/pimoroni_pico_import.cmake)
endif()

# set up project and SDK
project(${PROJECT} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# the lookup tables are generated into the build tree rather than the
# source tree, so that separate build directories (RP2040 vs RP2350,
# say) each get their own copy instead of fighting over one.
#
# these run at build time, so editing a generator is enough to have
# make rebuild the tables - the CONFIG_* values above are baked into
# the command line, hence the dependency on this file as well

set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/tables)

add_custom_command(
	OUTPUT	${GENERATED}/data.c ${GENERATED}/data.h ${GENERATED}/settings.h
	COMMAND	${CMAKE_CURRENT_LIST_DIR}/utils/data.js ${GENERATED}
			${CONFIG_SAMPLE_RATE} ${CONFIG_WAVE_SHIFT} ${CONFIG_BUFFER_SIZE}
	DEPENDS	${CMAKE_CURRENT_LIST_DIR}/utils/data.js ${CMAKE_CURRENT_LIST_FILE}
	COMMENT	"Building data files"
	VERBATIM
)

add_custom_command(
	OUTPUT	${GENERATED}/waves.c
	COMMAND	${CMAKE_CURRENT_LIST_DIR}/utils/waves.js ${GENERATED}
	DEPENDS	${CMAKE_CURRENT_LIST_DIR}/utils/waves.js
	COMMENT	"Building wave tables"
	VERBATIM
)

add_executable(${PROJECT}
	src/main.cxx
	src/usb_descriptors.c
	src/audio.c
	src/engine.cxx
	src/voice.cxx
	src/channel.cxx
	src/envelope.cxx
	src/filter.cxx
	src/presets.c

	# listing the generated headers here too, not just the .c files,
	# is what orders generation ahead of every compile in the target
	${GENERATED}/data.c
	${GENERATED}/data.h
	${GENERATED}/settings.h
	${GENERATED}/waves.c
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
target_include_directories(${PROJECT} PRIVATE ${GENERATED})

target_compile_options(${PROJECT} PRIVATE -Wall -Werror -O3)

target_compile_definitions(${PROJECT} PRIVATE
	PICO_AUDIO_I2S_MONO_INPUT=0
	PICO_AUDIO_I2S_MONO_OUTPUT=0
	PICO_AUDIO_I2S_DATA_PIN=${CONFIG_I2S_DATA_PIN}
	PICO_AUDIO_I2S_CLOCK_PIN_BASE=${CONFIG_I2S_CLOCK_PIN_BASE}
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
