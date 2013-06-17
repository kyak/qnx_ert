QNX Target Support Package for Embedded Coder
=======

**Summary:**

QNX (http://www.qnx.com) is a Unix-like real-time operating system, aimed primarily at the embedded systems market.

Simulink (http://www.mathworks.com/products/simulink) is a leading environment for multidomain simulation and Model-Based Design.

Embedded Coder (http://www.mathworks.com/products/embedded-coder) allows you to generate C code and deploy your algorithms to target hardware.

This Embedded Coder Target Support Package was tested on Beagleboard xM running QNX 6.5.1.
It should also work with other QNX targets, provided they have a BSP. Minimal changes to Template Makefile are required in this case to reflect other compiler flags.

**Installation:**

Run qnx_setup.m.

**What this package already has:**

    Standalong execution driven by POSIX timers (multirate/multitasking)
    External Mode support
    Automatic download to target using FTP and start via TELNET

**What this package would like to have:**

    See TODO.