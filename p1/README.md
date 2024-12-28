# CS238P_Project1
JIT Compiled Expression Evaluator

## Team members 
Shuyuan Fu  
Yiyan Kong  
Hungyau Su

## Overview  
This program dynamically compiles and executes a mathematical expression. It accepts a mathematical expression (a string containing symbols: +, -, *, /, %,  (, ), value). The program generates a C source code file based on this expression and uses the `fork()`, `execv()`, and `waitpid()` system calls to invoke the GCC compiler, creating a dynamically loadable module. The compiled module is then loaded into memory using `dlopen()`, and the result of the expression is evaluated by looking up the relevant symbol with `dlsym()` and executing the compiled code.

## Usage
```
make
./cs238 "expression"
```
This program calculates the sigmoid function for a given expression value by default. If you only want to output the expression value (without applying the sigmoid function), follow these steps:

1. Open `main.c`.
2. Comment out line 97-98 by removing the `/**/` from the beginning to the end of these lines.
3. Add comments to lines 101-103 to disable the sigmoid calculation.
4. `make` and run `./cs238 "expression"`

After making these changes, the program will output the raw expression value instead of the sigmoid value.
## Valgrind
```
valgrind --leak-check=full --log-file=valgrind_report.txt ./cs238 "1 + 3 * (4 - 1)"
```
## Strace Output
```
strace ./cs238 "1 + 3 * (4 - 1)" > strace_report.txt 2>&1
```
