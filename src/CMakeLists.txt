cmake_minimum_required(VERSION 2.8)

project(rsp_tcp)

find_library( PTHREAD_LIB pthread )

find_library( MIRICS_SDR_LIB mirsdrapi-rsp )

add_executable( ${PROJECT_NAME}
    IPAddress.cpp IPAddress.h
    common.cpp common.h
    devices.cpp devices.h
    mir_sdr_device.cpp mir_sdr_device.h
    rsp_cmdLineArgs.cpp rsp_cmdLineArgs.h
    rsp_tcp.cpp rsp_tcp.h
  )

target_link_libraries( ${PROJECT_NAME} "${MIRICS_SDR_LIB}" "${PTHREAD_LIB}" )

install (TARGETS rsp_tcp DESTINATION bin)
