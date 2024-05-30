#clion use jlink:

tcp:localhost:50000
JLinkGDBServer
-singlerun -if swd -port 50000 -device STM32F411CE -rtos GDBServer/RTOSPlugin_FreeRTOS.dylib 