# Implementing-a-Linux-Bash-Shell-on-C
Using C to implement a Linux Bash Shell script.
For non-builtin commands, We are using the fork-exec-wait pattern to spawn child processes that can execute our commands. They can also get and set environment variables as we need.
The child process will spawn and call execvp which will run your command, with the provided arguments.
Shell has following features:
1) Can store the history of commands to a file:
./shell -h <filename>
2) Supports running a series of commands from a script file:
./shell -f <filename>
3) !history Prints out each command in the history, in order.
4) #<n> prints and executes the nth command in history.
5) !<prefix> Prints and executes the last command that has the specified prefix. If no match is found, the appropriate error is printed nothing is stored in the history.
6) cd <path> changes directory like normal cd in linux.
7) Logical Operators: &&, ||, ;.
8) Program runs in background if you include '&' at end of command.
9) ps runs like normal linux command.
10) Redirection operators >>, >, <. '>>' appends contents of a command to end of file. '>' outputs contents of a command to a file. '<' takes content from a file and passes it as input for a command.
11) Implements Signal handling. SIGKILL - kill <pid>, SIGSTOP - stop <pid>, SIGCONT - cont <pid>.                        
 
