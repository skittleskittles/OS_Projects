# CS238P_Project2
Userspace Dynamic Thread Scheduler

## Team members 
Shuyuan Fu  
Yiyan Kong  
Hungyau Su

## Overview
This project implements a dynamic thread scheduler library with an API similar to POSIX `pthread`, enabling the creation and cooperative scheduling of multiple threads. The library allows threads to voluntarily yield control, enabling smooth, controlled multitasking.

In the provided example, five threads are created, each executing the `_thread_` function with a unique string argument (e.g., "hello," "world," etc.). Each thread then prints its argument followed by an incrementing counter, demonstrating concurrent behavior as they execute in interleaved segments. This controlled concurrency mimics real-world multitasking, where tasks are performed seemingly in parallel.

## Usage
```
make
./cs238 
```

## Valgrind
```
valgrind --leak-check=full --log-file=valgrind_report.txt ./cs238
```

## Strace Output
```
strace ./cs238 > strace_report.txt 2>&1
```
