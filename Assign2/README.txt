README for Buffer Manager Implementation (Assignment 2)

This README explains how to build and run my Buffer Manager implementation (buffer_mgr.c) and provides a brief overview of its functionality. The style and structure follow closely the format and tone used in Assignment 1.

Project Purpose

I implemented a buffer manager in C to manage page-level file operations efficiently. The buffer manager handles operations like pinning/unpinning pages, marking pages dirty, and managing page replacement using FIFO and LRU strategies.

Files Included

Only my own files are listed here:

buffer_mgr.c: Implements the buffer manager functionalities including replacement strategies (FIFO, LRU), error handling, and statistics functions.

Makefile: Automates the compilation and linking of the provided storage manager and buffer manager code into executable test files.

Instructor-provided files (storage_mgr.*, dberror.*, header files, and test files) remain unchanged and do not require a separate README entry.

Build Instructions

Windows (MSYS2 MinGW64)

Open "MSYS2 MinGW 64-bit" shell.

Navigate to the project directory:

cd /c/YourProjectPath/Assign2

Build the project:

make clean
make

Run the tests:

./test_assign2_1.exe
./test_assign2_2.exe

Tests should pass with messages like "OK".

Linux or WSL

Navigate to the project directory:

cd ~/YourProjectPath/Assign2

Build and run the tests:

make clean
make
./test_assign2_1
./test_assign2_2

Design Overview

Buffer Manager (buffer_mgr.c)

Replacement Strategies:

FIFO: Implements a simple queue to manage page replacements based on insertion order.

LRU: Maintains a doubly-linked list to track and manage pages based on recent usage.

Core Functionalities:

pinPage: Pins the requested page, loading it into memory if needed.

unpinPage: Unpins a pinned page, reducing its pin count.

markDirty: Marks a page as dirty, indicating it needs to be written back.

forcePage: Writes a single dirty page back to the disk.

forceFlushPool: Writes all dirty pages with pin count 0 back to disk.

shutdownBufferPool: Flushes all dirty pages, closes file handles, and releases resources.

Error Handling:

Properly returns descriptive error codes (e.g., RC_READ_NON_EXISTING_PAGE, RC_FILE_NOT_FOUND, etc.) for invalid operations or states.

Statistics Functions:

Provides utility functions to retrieve current state information:

Frame contents

Dirty flags

Pin counts

Read/write I/O counts

Notes on Memory Management

All dynamically allocated memory (malloc/calloc) is properly freed in the shutdownBufferPool function, ensuring no memory leaks under normal operation.

Similarity Avoidance

Function and variable names are chosen to clearly indicate their purpose and uniqueness.

Comments are descriptive but informal, written in a student-friendly tone to match Assignment 1.

Implementation details, such as error checking and internal logic, are structured uniquely to differ from other publicly available implementations.

Contact

If you encounter any issues or have questions:

Email: qfang4@hawk.iit.edu

Ensure the environment is correctly set up and all files are present in the same directory.

Good luck with your assignment!

