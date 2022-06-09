# Table of Contents
* [Introduction](#intro)
* [About](#about)
* [Instruction table](#instructions)
* [Build](#build)
* [Usage](#usage)
* [Testing](#testing)


## Introduction {#intro}
This project is the final project in the 1st year of my university. I tried to collect everything I knew in this project. 
## About <a name="about"></a>
This program is a binary translator that translates
code generated by my own assembler language (look at the [processor](https://github.com/krisszzzz/proccessor) repository) in x86 instructions.
A binary translator is a program that translates executable files created on one architecture into an executable file created on another architecture. The simplest translator walks through the executable and translates each instruction into the corresponding instruction for another architecture. My translator translates the instructions and puts them in a buffer that is allocated in the C program. Further with the help of mprotect the buffer becomes executable, and code injection occurs. This principle is similar to the principle of JIT compilers, but such compilers are created for the purpose of dynamic (in real time) code optimization. I don't have this at the moment. Maybe I'll add it in the future.  

## Instruction table <a name="instructions"></a>

Below you can see the table, that show how all instructions from my assembler translates into x86 instructions:  
|                                     My assembler instructions                                      	| x86 Intel instructions                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        	|
|:--------------------------------------------------------------------------------------------------:	|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------	|
|                                     push rax<br>(push register)                                    	| movq qword [rsp], xmm1<br>sub rsp, 8                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          	|
|                                push [0]<br>(push value from memory)                                	| vmovq xmm5, qword [r13 + 8 * 0] ; r13 contain the buffer address begin<br>vmovq qword [rsp], xmm5<br>sub rsp, 8                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               	|
|                                  push 1<br>(push immediate value)                                  	| movabs r14, 0x3ff0000000000000 ; r14 used as a temp<br>mov qword [rsp], r14<br>sub rsp, 8                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     	|
|                                 pop rax<br>(pop value to register)                                 	| vmovq xmm1, qword [rsp + 8]<br>add rsp, 8                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     	|
|                                  pop [0]<br>(pop value to memory)                                  	| vmovq xmm5, qword [rsp + 8]<br>vmovq qword [r13 + 8 * 0], xmm5<br>add rsp, 8                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  	|
|                               pop<br>(pop value without destination)                               	| add rsp, 8                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    	|
|               add; sub; mul; div<br>(do arithmetic operation using value from stack)               	| vmovq xmm5, qword [rsp+8]<br>addsd; subsd; mulsd; divsd xmm5, qword [rsp+16]<br>add rsp, 8                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    	|
|                                    jmp :Label<br>(jump to label)                                   	| jml rel32                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     	|
| je :Label ; ja :Label; jb :Label<br>(conditionally jump to label, compare two value<br>from stack) 	| vmovq xmm0, qword [rsp+8]<br>vmovq xmm5, qword [rsp+16]<br>add rsp, 16<br><br>vcmppd xmm5, xmm0, xmm5, 0 (in je case) ;<br>vcmppd xmm5, xmm0, xmm5, 0x1E (in ja case) ;<br>vcmppd xmm5, xmm0, xmm5, 0x11 (in jb case)<br>movmskpd r14d, xmm5<br>cmp r14d, 0x3 (in je case)<br>cmp r14d, 0x1 (in ja and jb case)<br>je rel32<br><br>(0x3 = 00000011b - the first bit is set if the lower value in xmm5 equal to<br> lower value of xmm0. But because the higher value of all xmm registers we zeroed<br> the second bit always will be set in je case. But it is not correct to ja and jb,<br> so the mask for ja and jb just 0x1 = 00000001b) 	|
|             call :Label<br>(jump to label and push the return address to <br>the stack)            	| mov [rsp], ret_address<br>sub rsp, 16; (align stack to avoid segfault)<br>jmp rel32                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           	|
|                           ret<br>(return to address in top of the stack)                           	| add rsp, 8 ; (Require because of alignment)<br>ret                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            	|
|                                      hlt<br>(end the program)                                      	| mov rsp, rbp ; (Restore the rsp value saved in the beginning)<br>ret                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          	|
|                           sqrt<br>(take root from the top value of stack)                          	| vmovq xmm0, qword [rsp+8]<br>vsqrtpd xmm0, xmm0<br>vmovq [rsp+8], xmm0                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        	|
|                      out<br>(output double precision float value using printf)                     	| lea rdi, [rsp + 8] ; Set the address of float value that will be outputed<br><br>vmovq qword [rsp], xmm1<br>sub rsp, 8<br>vmovq qword [rsp], xmm2<br>sub rsp, 8<br>vmovq qword [rsp], xmm3<br>sub rsp, 8<br>vmovq qword [rsp], xmm4<br>sub rsp, 8<br>; Save all register<br>call double_printf ; Calls a printf wrapper that prints a floating point number.<br>                   ; See the documentation<br>vmovq xmm4, qword [rsp+8]<br>add rsp, 8<br>vmovq xmm4, qword [rsp+8]<br>add rsp, 8<br>vmovq xmm4, qword [rsp+8]<br>add rsp, 8<br>vmovq xmm4, qword [rsp+8]<br>add rsp, 8<br>; Restore all register                              	|
|                       in<br>(input double precision float value using scanf)                       	| mov rdi, rsp ; Set the address of floating value that will be inputed<br>call double_scanf ; Calls a scanf wrapper that take a floating point  number.<br>                  ; See the documentation<br>sub rsp, 8                                                                                                                                                                                                                                                                                                                                                                                                                             	|
## Build <a name="build"></a>
~~~
git clone https://
cd ~/Binary_Translator
mkdir build
cd build
cmake ..
~~~

## Usage <a name="usage"></a>
Please go to the processor repository and read more info about usage of my [assembler language](https://github.com/krisszzzz/proccessor)  
Make an executable to my own processor.  

In *Binary Traslator* directory:   
Traslate executable and natively (on x86 Intel) execute the translated program:
~~~shell
./BT /path_to_file/
~~~

Execute the file in non-native processor (emulator wrotten by me):
~~~shell
./BT --non-native /path_to_file/
~~~

Write the execution and (in native mode) translation time:
~~~shell
./BT --time /path_to_file/
~~~

## Testing <a name="testing"></a>
I wrote I simple program in my assembler that count the factorial of number 50 10000 times and compare native and non-native execution.  
<details>

<summary>Source code</summary>

~~~
:5
push 50
pop ax

push 0
pop bx

push 1
pop [0]

:3
push bx
push 1
add
pop bx
push bx
push bx

push [0]
mul
pop [0]

push ax
ja :3

push cx
push 1
add
pop cx
push cx
push 10000
ja :5

push [0]
out
hlt 
~~~
</details>
</br>

Everywhere I used following flags:  
~~~
gcc -D NDEBUG -Ofast -mavx2 
~~~

Results:  
| Emulator | Native |
| -------- | ------ |
|  2810 ms |  4 ms  |

Accelaration is about 700 times.  
