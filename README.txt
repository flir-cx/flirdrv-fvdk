This repository defines a kernel driver used by flir pingu-linux 

The driver; fvdk (FLIR video driver, kernel module) is responsible for
loading fpga firmware (NECO boards) or at least setting up a fpga link
using the bifrost driver (another flir linux kernel driver) and keeping 
user space driver data in its memory

(A somewhat higher level of fpga communication is defined in user space
Common/common_fvdc library)
