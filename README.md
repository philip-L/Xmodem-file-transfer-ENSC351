# Xmodem-file-transfer-ENSC351

This project applies knowledge in multithreading programming implementing a FTP on a Linux operating system (XUbuntu). This code must run on Linux.

## Part 1
Basic file transfer implementing only the sender using Xmodem FTP.


## Part 2
Added the implementation of the receiver, a medium to simulate errors, and a main function to create threads for the sender and receiver. Also update myio to create socket pairs.


## Part 3
Updated myio to handle more errors (mainly spurious characters, extra ACK's) using mutex locks and condition variables.


## Part 4
Created the complete statechart for the receiver and sender modules. Can use Smartstate to generate the code for these diagrams.


## Part 5
Modified myio to use a thread-safe, lockless circular buffer which uses atomic variables.
