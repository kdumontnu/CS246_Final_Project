# CS246_Final_Project - Value Prediction Unit
Final project for CS246 - Advanced Computer Architecture

This is a simulation of a Value Prediction Unit (VPU) as introduced in the *Exceeding the Dataflow Limit via Value
Prediction* [Mikko H. Lipasti et. all, 1996], using [Intel Pin Utility](https://software.intel.com/en-us/articles/pin-a-dynamic-binary-instrumentation-tool)

The basic structure includes a Value Prediction Table (VPT), containing the previous write values for each address indexed by the lowest *n* bits of the instruction pointer (PC), and a Classification Table (CT) containing an *y* bit saturating counter that represents the likelihood of a positive prediction, indexed by the lowest *m* bits of PC address

Values for *m*,*n*, and *y* are all configurable knob parameters. 

Later versions of this VPU includes an implimentation of variable-depth value histories, where multiple previous values can be stored in the VPT under a Least Recently Used (LRU) eviction policy.

The most recent version of this VPU adds the option to add a Victim Cache to the the VPT. The victim cache stores values evicted from the VPT with an *x* bit LRU stack. 

## Installing and Running

install [Intel Pin Utility](https://software.intel.com/en-us/articles/pin-a-dynamic-binary-instrumentation-tool)
```
make
pin -t obj-intel64/main.so -outfile results.out -size 10 -- /bin/ls
```
To create a 1024 entry VPT (and default 256 entry), and simulate on `/bin/ls` bash command

**CLI arguments**

ARG|DEFAULT|DESCRIPTION
---|-------|-----------
"outfile"  | "tool.out"| "Output file for the pintool" 
"pid"| "0" | "Append pid to output"
"inst_limit"| "1000000000"| "Quit after executing x number of instructions"
"inst_cat"| "ALL"| "What category of instructions?" (Not supported)
"size"| "8" | "Size of Value Prediction table in bits. Total length = 2**size"
"CTbits"| "1"| "Size of CT prediction history counter in bits"
"CTsize"| "8"| "Size of Classification table in bits. Total length = 2**size""
"HistDepth"| "1"| "Value history size"
"VictimCache"| "0"| "Entries in victim cache"

Instructions are catagorized by the following for processing:

Instruction Category|Description
---|---
I_PURE_LOAD| Integer Pure Load Instruction (e.g. mov esi, dword ptr [rsi] )
I_LOAD_ARITH| Integer Load + Arithmetic Instruction (e.g. add r8, qword ptr [rsi+0x10] )
I_ARITH_1OP| Integer Pure Arithmetic Instruction, 1 operand (e.g add rcx, 0x40 )
I_ARITH_2OP| Integer Pure Arithmetic Instruction, 2 operand (e.g add rcx, rax )
I_REG_MOV| Integer Register Move Instruction (e.g. mov rax, rdi )
F_PURE_LOAD| Floating Point Pure Load Instruction (e.g. movdqa xmm5, xmmword ptr [rdi] )
F_LOAD_ARITH| Floating Point Load with Arithmetic Instruction (e.g. pminub xmm4, xmmword ptr [rdi+0x30] )
F_PURE_ARITH| Floating Point Pure Arithmetic Instruction (e.g. paddd xmm0, xmm6 )
F_REG_MOVE| Floating Point Register Move Instructions (e.g. movd xmm0, esi )
UNKNOWN| Anything not classified above

## Results

Full results can be found here: [Report](https://github.com/kdumontnu/CS246_Final_Project/blob/master/CS246%20Final%20Report%20%5BKD%20%26%20DL%5D.pdf)

**Instruction Value Locality**

The following bars indicated the % of cases where the output of an operation was the same as the previous time the operation was executed (i.e. history depth of 1). The dashed line shows the % of instructions seen for each catagory. 

![Value Locality](https://github.com/kdumontnu/CS246_Final_Project/blob/master/img/value_locality_by_inst.PNG "Value Locality by Instruction Type")

We used a simple calculation to model the speedup for an architecture implementing our Value Prediction Unit. The maximum possible speedup under this would be 2x.

![Speedup Calc](https://github.com/kdumontnu/CS246_Final_Project/blob/master/img/speedup_calc.PNG "Speedup Calculation")

![Speedup Results](https://github.com/kdumontnu/CS246_Final_Project/blob/master/img/Speedup.PNG "Speedup Results")

![Speedup Config](https://github.com/kdumontnu/CS246_Final_Project/blob/master/img/speedup_configs.PNG "Speedup Configurations")


