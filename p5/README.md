# System-Performance-Monitor

## Team members

Shuyuan Fu  
Yiyan Kong  
Hungyau Su

## Overview

A real-time system monitoring tool similar to the Unix top command that provides two different performance statistics in four different categories (CPU, memory, network, I/O).

## Usage

1. Build the project:
    ```
    make
    ```
2. Run the file system:
    ```
    ./cs238
    ```
3. End the system:
    ```
    Control+C (Ctrl+C)
    ```

## Valgrind

```
valgrind --leak-check=full --log-file=valgrind_report.txt ./cs238
```

## Strace Output

```
strace -o trace_output.txt ./cs238
```
