cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(gb_interceptor C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(gb_interceptor)

target_sources(gb_interceptor PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/main.c
	${CMAKE_CURRENT_LIST_DIR}/cpubus.c
	${CMAKE_CURRENT_LIST_DIR}/opcodes.c
	${CMAKE_CURRENT_LIST_DIR}/ppu.c
	${CMAKE_CURRENT_LIST_DIR}/jpeg/jpeg.c
	${CMAKE_CURRENT_LIST_DIR}/osd.c
	${CMAKE_CURRENT_LIST_DIR}/debug.c
	${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
	${CMAKE_CURRENT_LIST_DIR}/gamedb/game_detection.c
	)

target_link_libraries(gb_interceptor PUBLIC pico_stdlib hardware_pio pico_multicore hardware_interp hardware_dma tinyusb_device tinyusb_board pico_unique_id)

target_include_directories(gb_interceptor PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_compile_definitions(gb_interceptor PUBLIC PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

pico_add_extra_outputs(gb_interceptor)

pico_enable_stdio_usb(gb_interceptor 1)
pico_enable_stdio_uart(gb_interceptor 0)

pico_set_binary_type(gb_interceptor copy_to_ram)

pico_generate_pio_header(gb_interceptor
        ${CMAKE_CURRENT_LIST_DIR}/memory-bus.pio
)

pico_generate_pio_header(gb_interceptor
		${CMAKE_CURRENT_LIST_DIR}/jpeg/jpeg_prepare.pio
)

pico_generate_pio_header(gb_interceptor
		${CMAKE_CURRENT_LIST_DIR}/jpeg/jpeg_encoding.pio
)

if (DEFINED BASE_VIDEO_MODE)
	MESSAGE(STATUS "Building BASE_VIDEO_MODE variant")
	add_compile_definitions("BASE_VIDEO_MODE") #Uncomment for base video mode version with fixed 30fps and no frame blending
endif()
