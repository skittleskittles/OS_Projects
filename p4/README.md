# CS238_Project4
Key/Value File System

## Team members

Shuyuan Fu  
Yiyan Kong  
Hungyau Su

## Overview

A key/value file system that utilizes raw and direct I/O operations on a block device.

## Usage

Enable the ability to store and restore the state of the key/value file system on open/close by modifing the `STORE_RESTORE_FILE` macro in `<system.h>`:
```C
/* <system.h> */

#ifndef _SYSTEM_H_
#define _SYSTEM_H_
...
#define STORE_RESTORE_FILE 1 // 0: disable, 1: enable
```

Then run the following commonds:

1. Build the project:
    ```
    make
    ```
2. Create a block file:
    ```
    dd if=/dev/zero of=file bs=4096 count=100000
    ```
3. Set up a loopback device:
    ```
    sudo losetup /dev/loop0 file
    ```
4. Run the file system:
    ```
    ./cs238 ./file
    ```
5. Before shutting down the machine, detach the loopback device:
    ```
    sudo losetup -d /dev/loop0
    ```

## Valgrind

```
valgrind --leak-check=full --log-file=valgrind_report.txt ./cs238 ./file
```

## Strace Output

```
strace -o trace_output.txt ./cs238 ./file
```
