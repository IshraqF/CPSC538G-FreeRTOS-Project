add_executable(${ProjectName}
    main.c
)

target_include_directories(${ProjectName} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(${ProjectName} 
    pico_stdlib 
    FreeRTOS-Kernel-Heap4 
    )

pico_add_extra_outputs(${ProjectName})

# Route printf to UART0 (GP0=TX, GP1=RX) so the Debug Probe can capture it.
# USB stdio is disabled to avoid needing a separate USB connection.
pico_enable_stdio_uart(${ProjectName} 1)
pico_enable_stdio_usb(${ProjectName} 0)