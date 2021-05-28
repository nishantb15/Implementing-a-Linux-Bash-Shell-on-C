# Implementing-a-Linux-Bash-Shell-on-C
Using C to implement a Linux Bash Shell script.
We are using the fork-exec-wait pattern to spawn child processes that can execute our commands. They can also get and set environment variables as we need.
The child process will spawn and call execvp which will run your command, with the provided arguments.
Shell has following features:
1) Can store the history of commands to a file:
./shell -h <filename>
2) Supports running a series of commands from a script file:
./shell -f <filename>
3) !history Prints out each command in the history, in order.
4) #<n> prints and executes the nth command in history.
5) !<prefix> Prints and executes the last command that has the specified prefix. If no match is found, print the appropriate error and do not store anything in the history.
