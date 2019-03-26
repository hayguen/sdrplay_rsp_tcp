# RSP_TCP

RSP_tcp - TCP/IP I/Q Data Server for the sdrplay RSP2

Copyright (C) 2017 Clem Schmidt, softsyst GmbH, http://www.softsyst.com

## Build

- download and install the API/HW driver from SDRplays Download page https://www.sdrplay.com/downloads/
    * wget https://www.sdrplay.com/software/SDRplay_RSP_API-RPi-2.13.1.run

- get the sources
    * git clone https://github.com/hayguen/sdrplay_rsp_tcp.git

- create build directory for object files
    * mkdir build_rsp_tcp

- switch into this build directory
    * cd build_rsp_tcp

- configure cmake
    * cmake ../sdrplay_rsp_tcp

- start compilation
    * make

- install
    * sudo make install

