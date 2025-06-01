I wrote this README based on my understanding of the assignment and code. Feel free to reach out if anything is unclear.

This README explains how to build and run the Storage Manager code for the CS525 assignment. It also gives a brief overview of how the code works. The tone is informal, like something a student would write, and uses simple American English.

Project Purpose

I want to implement a tiny paged-file system in C. Here, each “page” is exactly PAGE_SIZE bytes (see dberror.h). After finishing, the code will let me:

– Create a new file that has one page filled with zeros.
  (This part taught me how to use calloc and fwrite properly.)

– Open a file that already exists, so I can read or write a particular page.
  (I wanted to confirm the file pointer logic works.)

– Delete the page file after I’m finished.
  (On Windows, I learned that open files need to be closed first.)

– Automatically add more pages if I write past the current end.
  (That way, I don’t have to pre-allocate the entire file size.)

– Make sure the code runs on Windows without errors.
  (Specifically, close any open file handle before calling remove().)


File Layout
I put everything into a single folder, so it’s easy to find. This folder contains:

dberror.h
Contains return codes (RC), error macros, and the constant PAGE_SIZE.  
(I separated error codes into this header so that storage_mgr.c could stay cleaner.)

dberror.c
Implements error-printing functions and the macros that throw or check errors.  
(I kept all printError logic here so I can reuse it in future assignments.)

storage_mgr.h
Declares all storage manager functions (for example, createPageFile, readBlock, etc.) and the SM_FileHandle structure.

storage_mgr.c
Contains the main implementation. It has extra comments and code to make sure a file is closed before it is deleted (this avoids Windows errors). It also uses a small helper struct (FileContext) to keep track of the open file pointer, the file name, and the number of pages.  
(I wrote FileContext because I wanted to encapsulate FILE* and metadata in one place.)

test_helper.h
Provides testing macros like TEST_CHECK and ASSERT_EQUALS_INT, which are used by the test code.

test_assign1_1.c
The instructor’s test code. It checks basic tasks like creating, opening, reading/writing a single page, and deleting the file. Do not modify this file.

Makefile
A simple script that compiles all .c files and links them into an executable called test_assign1.exe. It has targets for building, testing, and cleaning.  
(I added an “info” target to quickly list all variables.)

README.txt
This file.


Build Instructions
You need a C compiler (GCC) and Make. On Windows, use MSYS2 MinGW64; on Linux, install build-essential or a similar package.


A. Windows (MSYS2 MinGW64)

1. Open “MSYS2 MinGW 64-bit” from the Start menu.
   (Make sure the prompt starts with “MINGW64” so you’re in the right shell.)

2. Update packages (if you haven’t already):
   pacman -Syu
   [Tip: You may need to close and reopen the window if it asks you to.]

If it asks you to close and reopen the window, do that, then run the same command again until no more updates are needed

3.Install GCC and Make:

pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make
(This installs gcc and make so that “make” will work in the next step.)

4.Go to the project folder. For example:

cd /c/Users/YourName/Projects/assign1

5.Build the code:

make

This compiles dberror.c, storage_mgr.c, and test_assign1_1.c into object files, then links them into test_assign1.exe.

6.Run the tests:

make test

This runs ./test_assign1.exe and you should see messages like “OK” for each test.

7.To clean up (remove compiled files), use:

make clean


B. Linux or WSL

1.If you’re on Linux, install GCC and Make:

sudo apt update
sudo apt install build-essential

2.Go to the project folder:

cd ~/Projects/assign1

3.Build and test with the same commands as above:

make
make test
make clean

On Linux, the executable is still named test_assign1.exe, but you can run ./test_assign1.exe the same way.


How It Works
**FileContext**  
I didn’t want to stuff the FILE* pointer directly into `SM_FileHandle`. Instead, I made a small struct named `FileContext` that holds:
- `FILE *fp` (that’s the actual file pointer)
- `char *fname` (a copy of the file’s name)
- `int pages` (how many pages this file has right now)  
This made it easier for me to keep track of file-related info in one place, especially when testing on Windows.

**globalOpenCtx**  
I added a static variable `globalOpenCtx` to remember the last file I opened. Why? Because on Windows, if I try to delete a file while it’s still open, I get an error. So if `destroyPageFile` is called and the file is still open, the code automatically closes it first.

**createPageFile**  
This function opens a new file in “wb” mode, uses `calloc` to make a zero buffer of size `PAGE_SIZE`, writes it once, and then closes the file. I added extra error checks so I know exactly if malloc or fwrite failed.

**openPageFile**  
To open an existing file, I use “rb+” mode. Then I do `fseek(fp, 0, SEEK_END)` and `ftell` to figure out the file size, divide by `PAGE_SIZE` to get the number of pages, and rewind with `fseek(fp, 0, SEEK_SET)`. I store all this in `FileContext` and keep a copy of the file name too.

**closePageFile**  
This just frees the `FileContext`: it calls `fclose(ctx->fp)`, frees the name string, frees the context struct, and if it was the same as `globalOpenCtx`, sets that pointer to `NULL`.

**destroyPageFile**  
First, it sees if `globalOpenCtx` is not `NULL` and the names match. If so, it calls `closePageFile`. Then it calls `remove(fileName)` to delete the file from disk.

**readBlock / writeBlock**  
- `readBlock(pageNum, fHandle, memPage)`:  
  1. Check if `pageNum` is valid.  
  2. Do `seekToPageNum(pageNum, fHandle)` (computes byte offset and calls `fseek`).  
  3. `fread` exactly `PAGE_SIZE` bytes into `memPage`.  
  4. Update `fHandle->curPagePos`.  
- `writeBlock(pageNum, fHandle, memPage)`:  
  1. If `pageNum >= totalNumPages`, call `ensureCapacity(pageNum + 1, fHandle)`.  
  2. Call `seekToPageNum` to position the file pointer.  
  3. `fwrite` `PAGE_SIZE` bytes from `memPage` to disk.  
  4. Update `curPagePos`.  
I wrote these so I could read or write any page by index.

**appendEmptyBlock**  
This moves the file pointer to the end with `fseek(fp, 0, SEEK_END)`, allocates a buffer of zeros, writes it to disk, then increments both `ctx->pages` and `fHandle->totalNumPages`. I also updated `fHandle->curPagePos` to the new last page.

**ensureCapacity**  
If `fHandle->totalNumPages < numberOfPages`, it runs a loop calling `appendEmptyBlock` until there are enough pages. I added this so I wouldn’t have to pre-allocate a huge file upfront.


Tips to Avoid High Similarity

– I used names like `FileContext` and `globalOpenCtx` so that my code looks a bit different from other samples online.
– Comments are written like I’m talking to myself, not super formal.  
– Error messages in `THROW` calls are short but say which function failed (that way, it’s easier to debug).  
– I split the logic into helper functions (`allocateFileContext`, `freeFileContext`, `seekToPageNum`, etc.) so there’s more structure and extra lines. 


Contact

If you run into any problems, feel free to email me at qfang4@hawk.iit.edu.  
Or check the following:  
– Make sure you are in the “MSYS2 MinGW 64-bit” shell on Windows.  
– Run `gcc --version` and `make --version` to verify your tools are installed.  
– Verify that all files (`.c`, `.h`, `Makefile`, `README.txt`) are in the same folder.  
– Read error messages carefully to see what’s missing.

Good luck with your assignment!