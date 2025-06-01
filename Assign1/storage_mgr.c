
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage_mgr.h"
#include "dberror.h"

/* We hardcode the page size from dberror.h for convenience */
#define PAGE_SIZE_BYTES PAGE_SIZE

/*
 * Internal data structures
 */

/*
 * FileContext
 *
 * A wrapper struct that holds additional information for an open file,
 * beyond what the SM_FileHandle provides. We store:
 *   - fp: the actual FILE* pointer used for I/O.
 *   - fname: a dynamically allocated copy of the file name.
 *   - pages: total number of pages currently known for this file.
 *
 * This allows us to centralize all file-related bookkeeping in one place.
 */
typedef struct FileContext {
    FILE *fp;           /* Underlying file pointer for I/O */
    char *fname;        /* Dynamically allocated file name */
    int pages;          /* Number of pages currently in the file */
} FileContext;

/* 
 * We keep track of the one “last opened” context so that if
 * destroyPageFile is called while it is still open, we can
 * automatically close it before deletion (necessary on Windows).
 */
static FileContext *globalOpenCtx = NULL;

/*
 * Forward declarations of internal helper functions
 */

/* Allocate a new FileContext for a given file name and FILE* */
static FileContext* allocateFileContext(const char *fileName, FILE *fp, int totalPages);

/* Free a FileContext, closing fp and freeing memory */
static RC freeFileContext(FileContext *ctx);

/* Seek the underlying FILE* to the byte offset for the given page number */
static RC seekToPageNum(int pageNum, SM_FileHandle *fHandle);

/*
 * initStorageManager
 *
 * Called once before any other storage manager operation. In our simple
 * case, we have no global state to initialize aside from ensuring
 * the globalOpenCtx is NULL. 
 */
void initStorageManager(void) {
    /* Just ensure the global context pointer starts cleared */
    globalOpenCtx = NULL;
}

/*
 * createPageFile
 *
 * Create a brand-new page file with a single zero-filled page.
 * Steps:
 *   1. Open the file for writing in “wb” mode (create or truncate).
 *   2. Allocate a PAGE_SIZE_BYTES block of zeros in memory.
 *   3. Write that block once to the file.
 *   4. Close the file pointer.
 *
 * Returns:
 *   - RC_OK on success.
 *   - RC_WRITE_FAILED if any I/O or memory allocation fails.
 */
RC createPageFile(char *fileName) {
    /* Attempt to open (or create) the file in binary write mode */
    FILE *fp = fopen(fileName, "wb");
    if (fp == NULL) {
        /* Cannot create or open file for writing */
        THROW(RC_WRITE_FAILED, "createPageFile: failed to open file for writing");
    }

    /* Allocate a zeroed-out buffer of PAGE_SIZE_BYTES */
    char *zeroBuf = (char *) calloc(PAGE_SIZE_BYTES, sizeof(char));
    if (zeroBuf == NULL) {
        /* Memory allocation failed; close file and report error */
        fclose(fp);
        THROW(RC_WRITE_FAILED, "createPageFile: failed to allocate zero buffer");
    }

    /* Write exactly one page of zeros */
    size_t written = fwrite(zeroBuf, sizeof(char), PAGE_SIZE_BYTES, fp);
    free(zeroBuf);
    if (written < PAGE_SIZE_BYTES) {
        /* Could not write the full page; close and error out */
        fclose(fp);
        THROW(RC_WRITE_FAILED, "createPageFile: failed to write full zero page");
    }

    /* Flush to ensure data is on disk, then close */
    fflush(fp);
    fclose(fp);
    return RC_OK;
}

/*
 * openPageFile
 *
 * Open an existing page file and initialize the provided SM_FileHandle.
 * Steps:
 *   1. Try to open with mode “rb+” (read/update). If that fails, report RC_FILE_NOT_FOUND.
 *   2. fseek(fp, 0, SEEK_END) and ftell to determine total file size.
 *   3. Compute totalPages = fileSize / PAGE_SIZE_BYTES.
 *   4. Allocate a FileContext that stores the FILE* and file name copy.
 *   5. Populate fHandle->fileName, totalNumPages, curPagePos=0, and mgmtInfo = context.
 *   6. Remember context in globalOpenCtx for later potential destroyPageFile handling.
 *
 * Returns:
 *   - RC_OK on success.
 *   - RC_FILE_NOT_FOUND if fopen fails.
 *   - RC_READ_NON_EXISTING_PAGE if lseek/ftell fails unexpectedly.
 */
RC openPageFile(char *fileName, SM_FileHandle *fHandle) {
    if (fileName == NULL || fHandle == NULL) {
        THROW(RC_FILE_NOT_FOUND, "openPageFile: null arguments provided");
    }

    /* Open existing file in read+write mode (binary) */
    FILE *fp = fopen(fileName, "rb+");
    if (fp == NULL) {
        /* Cannot find or open the file */
        THROW(RC_FILE_NOT_FOUND, "openPageFile: file does not exist");
    }

    /* Seek to end to compute size */
    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        THROW(RC_READ_NON_EXISTING_PAGE, "openPageFile: cannot seek to end");
    }
    long fileSizeBytes = ftell(fp);
    if (fileSizeBytes < 0) {
        /* ftell failed */
        fclose(fp);
        THROW(RC_READ_NON_EXISTING_PAGE, "openPageFile: cannot obtain file size");
    }

    /* Compute number of whole pages in the file */
    int totalPages = (int)(fileSizeBytes / PAGE_SIZE_BYTES);

    /* Create a copy of the fileName inside the handle */
    char *nameCopy = (char *) malloc(strlen(fileName) + 1);
    if (nameCopy == NULL) {
        fclose(fp);
        THROW(RC_FILE_HANDLE_NOT_INIT, "openPageFile: memory allocation failed for fileName");
    }
    strcpy(nameCopy, fileName);

    /* Allocate our FileContext wrapper */
    FileContext *ctx = allocateFileContext(nameCopy, fp, totalPages);
    if (ctx == NULL) {
        free(nameCopy);
        fclose(fp);
        THROW(RC_FILE_HANDLE_NOT_INIT, "openPageFile: failed to allocate FileContext");
    }

    /* Initialize the SM_FileHandle fields */
    fHandle->fileName     = nameCopy;
    fHandle->totalNumPages = totalPages;
    fHandle->curPagePos    = 0;           /* start at first page */
    fHandle->mgmtInfo      = (void *) ctx;

    /* Rewind the file pointer to the beginning for consistent read/write */
    fseek(fp, 0L, SEEK_SET);

    /* Store this context globally in case destroyPageFile is called prematurely */
    globalOpenCtx = ctx;
    return RC_OK;
}

/*
 * closePageFile
 *
 * Close the open file and free any resources. Steps:
 *   1. Extract the FileContext from fHandle->mgmtInfo.
 *   2. fclose the FILE* inside context.
 *   3. free the filename string in fHandle and free the context struct.
 *   4. Clear fHandle fields (fileName, mgmtInfo) to prevent double-close.
 *   5. If this context matches globalOpenCtx, clear globalOpenCtx as well.
 *
 * Returns:
 *   - RC_OK on success.
 *   - RC_FILE_HANDLE_NOT_INIT if fHandle or mgmtInfo is NULL.
 */
RC closePageFile(SM_FileHandle *fHandle) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "closePageFile: file handle not initialized");
    }

    FileContext *ctx = (FileContext *) fHandle->mgmtInfo;
    RC rc = freeFileContext(ctx);
    if (rc != RC_OK) {
        /* freeFileContext will report errors via THROW if needed */
        return rc;
    }

    /* Free the fileName stored in fHandle and reset mgmtInfo */
    free(fHandle->fileName);
    fHandle->fileName = NULL;
    fHandle->mgmtInfo = NULL;
    fHandle->totalNumPages = 0;
    fHandle->curPagePos = 0;

    /* If this was our global context, clear it */
    if (globalOpenCtx == ctx) {
        globalOpenCtx = NULL;
    }

    return RC_OK;
}

/*
 * destroyPageFile
 *
 * Delete a page file from disk. On Windows, if the file is still open (i.e.,
 * globalOpenCtx points to a context whose filename matches), we must close it
 * before calling remove(). Steps:
 *   1. Check if globalOpenCtx != NULL and its fname matches fileName. If so, close it.
 *   2. Attempt remove(fileName).
 *   3. Return RC_OK if successful; otherwise RC_FILE_NOT_FOUND.
 *
 * Returns:
 *   - RC_OK on success.
 *   - RC_FILE_NOT_FOUND if remove fails.
 *   - potentially RC_FILE_HANDLE_NOT_INIT if closing fails.
 */
RC destroyPageFile(char *fileName) {
    if (fileName == NULL) {
        THROW(RC_FILE_NOT_FOUND, "destroyPageFile: null fileName");
    }

    /* If there's a still-open context for the same filename, close it first */
    if (globalOpenCtx != NULL && strcmp(globalOpenCtx->fname, fileName) == 0) {
        /* Simulate fHandle by constructing a temporary SM_FileHandle */
        SM_FileHandle tempHandle;
        tempHandle.fileName = globalOpenCtx->fname;
        tempHandle.mgmtInfo = (void *) globalOpenCtx;
        tempHandle.totalNumPages = globalOpenCtx->pages;
        tempHandle.curPagePos = 0;  /* not used in closePageFile itself */

        /* Force close */
        RC rcClose = closePageFile(&tempHandle);
        if (rcClose != RC_OK) {
            /* If close fails, propagate the error */
            return rcClose;
        }
        /* globalOpenCtx cleared in closePageFile */
    }

    /* Now attempt to delete the file from disk */
    if (remove(fileName) != 0) {
        /* Could not delete (either non-existent or locked) */
        THROW(RC_FILE_NOT_FOUND, "destroyPageFile: failed to remove file");
    }

    return RC_OK;
}

/*
 * readBlock
 *
 * Read a specific page numbered pageNum (0-based) from disk into memPage.
 * Steps:
 *   1. Validate fHandle and its mgmtInfo.
 *   2. Ensure pageNum is < totalNumPages (otherwise THROW RC_READ_NON_EXISTING_PAGE).
 *   3. Seek to the byte offset for that page.
 *   4. fread exactly PAGE_SIZE_BYTES into memPage.
 *   5. If fread returns fewer bytes, error out.
 *   6. Update curPagePos in fHandle.
 *
 * Returns:
 *   - RC_OK on success.
 *   - RC_FILE_HANDLE_NOT_INIT if handle is null or not opened.
 *   - RC_READ_NON_EXISTING_PAGE if pageNum invalid or I/O fails.
 */
RC readBlock(int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "readBlock: file handle not initialized");
    }
    if (pageNum < 0 || pageNum >= fHandle->totalNumPages) {
        THROW(RC_READ_NON_EXISTING_PAGE, "readBlock: pageNum out of bounds");
    }

    /* Seek to the correct page offset in bytes */
    RC rcSeek = seekToPageNum(pageNum, fHandle);
    if (rcSeek != RC_OK) {
        THROW(RC_READ_NON_EXISTING_PAGE, "readBlock: seek to page failed");
    }

    FileContext *ctx = (FileContext *) fHandle->mgmtInfo;
    size_t actuallyRead = fread(memPage, sizeof(char), PAGE_SIZE_BYTES, ctx->fp);
    if (actuallyRead < PAGE_SIZE_BYTES) {
        THROW(RC_READ_NON_EXISTING_PAGE, "readBlock: could not read full page");
    }

    /* Update current page position in the handle */
    fHandle->curPagePos = pageNum;
    return RC_OK;
}

/*
 * getBlockPos
 *
 * Simply return the current page position stored in the file handle.
 */
int getBlockPos(SM_FileHandle *fHandle) {
    if (fHandle == NULL) {
        return -1; /* Invalid handle */
    }
    return fHandle->curPagePos;
}

/*
 * readFirstBlock
 *
 * Read the page at index 0 into memPage. Effectively just calls readBlock(0, ...).
 */
RC readFirstBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(0, fHandle, memPage);
}

/*
 * readPreviousBlock
 *
 * Read the page immediately before the current position.
 * Steps:
 *   1. Compute prev = curPagePos - 1.
 *   2. If prev < 0, THROW RC_READ_NON_EXISTING_PAGE.
 *   3. Call readBlock(prev, ...).
 */
RC readPreviousBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "readPreviousBlock: file handle not initialized");
    }
    int prev = fHandle->curPagePos - 1;
    if (prev < 0) {
        THROW(RC_READ_NON_EXISTING_PAGE, "readPreviousBlock: already at first page");
    }
    return readBlock(prev, fHandle, memPage);
}

/*
 * readCurrentBlock
 *
 * Read the page at the current page position.
 */
RC readCurrentBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "readCurrentBlock: file handle not initialized");
    }
    return readBlock(fHandle->curPagePos, fHandle, memPage);
}

/*
 * readNextBlock
 *
 * Read the page immediately after the current position.
 */
RC readNextBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "readNextBlock: file handle not initialized");
    }
    int next = fHandle->curPagePos + 1;
    if (next >= fHandle->totalNumPages) {
        THROW(RC_READ_NON_EXISTING_PAGE, "readNextBlock: already at last page");
    }
    return readBlock(next, fHandle, memPage);
}

/*
 * readLastBlock
 *
 * Read the last page in the file.
 */
RC readLastBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "readLastBlock: file handle not initialized");
    }
    int last = fHandle->totalNumPages - 1;
    return readBlock(last, fHandle, memPage);
}

/*
 * writeBlock
 *
 * Write the contents of memPage (PAGE_SIZE_BYTES) into page number pageNum.
 * Steps:
 *   1. Validate handle.
 *   2. If pageNum >= totalNumPages, call ensureCapacity(pageNum+1).
 *   3. Seek to the page offset.
 *   4. fwrite exactly PAGE_SIZE_BYTES from memPage into file.
 *   5. fflush to ensure write goes to disk.
 *   6. Update curPagePos.
 *
 * Returns:
 *   - RC_OK on success.
 *   - RC_FILE_HANDLE_NOT_INIT if uninitialized.
 *   - RC_WRITE_FAILED on any I/O error.
 */
RC writeBlock(int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "writeBlock: file handle not initialized");
    }
    if (pageNum < 0) {
        THROW(RC_WRITE_FAILED, "writeBlock: negative pageNum");
    }

    /* If writing beyond current end, extend capacity */
    if (pageNum >= fHandle->totalNumPages) {
        RC rcExtend = ensureCapacity(pageNum + 1, fHandle);
        if (rcExtend != RC_OK) {
            THROW(RC_WRITE_FAILED, "writeBlock: ensureCapacity failed");
        }
    }

    /* Seek to correct position in file */
    RC rcSeek = seekToPageNum(pageNum, fHandle);
    if (rcSeek != RC_OK) {
        THROW(RC_WRITE_FAILED, "writeBlock: seek to page failed");
    }

    FileContext *ctx = (FileContext *) fHandle->mgmtInfo;
    size_t written = fwrite(memPage, sizeof(char), PAGE_SIZE_BYTES, ctx->fp);
    if (written < PAGE_SIZE_BYTES) {
        THROW(RC_WRITE_FAILED, "writeBlock: could not write full page");
    }
    fflush(ctx->fp);

    /* Update the handle’s metadata */
    fHandle->curPagePos = pageNum;
    return RC_OK;
}

/*
 * writeCurrentBlock
 *
 * Write to the page at the current position.
 */
RC writeCurrentBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "writeCurrentBlock: file handle not initialized");
    }
    return writeBlock(fHandle->curPagePos, fHandle, memPage);
}

/*
 * appendEmptyBlock
 *
 * Append exactly one zero-filled page to the end of the file. Steps:
 *   1. fseek(fp, 0, SEEK_END).
 *   2. Allocate a zero buffer of PAGE_SIZE_BYTES.
 *   3. fwrite the buffer to the end.
 *   4. ffush, update totalNumPages in both context and fHandle.
 *   5. Update curPagePos to new last page index.
 *
 * Returns:
 *   - RC_OK on success.
 *   - RC_WRITE_FAILED if I/O or allocation fails.
 */
RC appendEmptyBlock(SM_FileHandle *fHandle) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "appendEmptyBlock: file handle not initialized");
    }

    FileContext *ctx = (FileContext *) fHandle->mgmtInfo;

    /* Move to end of file */
    if (fseek(ctx->fp, 0L, SEEK_END) != 0) {
        THROW(RC_WRITE_FAILED, "appendEmptyBlock: seek to end failed");
    }

    /* Allocate zero buffer for one page */
    char *zeroBuf = (char *) calloc(PAGE_SIZE_BYTES, sizeof(char));
    if (zeroBuf == NULL) {
        THROW(RC_WRITE_FAILED, "appendEmptyBlock: memory allocation failed");
    }

    /* Write the zero buffer */
    size_t written = fwrite(zeroBuf, sizeof(char), PAGE_SIZE_BYTES, ctx->fp);
    free(zeroBuf);
    if (written < PAGE_SIZE_BYTES) {
        THROW(RC_WRITE_FAILED, "appendEmptyBlock: failed to write full zero page");
    }
    fflush(ctx->fp);

    /* Update context and handle metadata */
    ctx->pages += 1;
    fHandle->totalNumPages = ctx->pages;
    fHandle->curPagePos    = ctx->pages - 1; /* last page index */

    return RC_OK;
}

/*
 * ensureCapacity
 *
 * Ensure that the file has at least numberOfPages pages. If current totalNumPages
 * < numberOfPages, repeatedly append empty pages until the requirement is met.
 *
 * Returns:
 *   - RC_OK on success.
 *   - RC_FILE_HANDLE_NOT_INIT if handle is null.
 *   - RC_WRITE_FAILED if any append fails.
 */
RC ensureCapacity(int numberOfPages, SM_FileHandle *fHandle) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "ensureCapacity: file handle not initialized");
    }
    if (numberOfPages < 0) {
        THROW(RC_WRITE_FAILED, "ensureCapacity: invalid numberOfPages");
    }

    /* Keep appending until we have at least numberOfPages pages */
    while (fHandle->totalNumPages < numberOfPages) {
        RC rc = appendEmptyBlock(fHandle);
        if (rc != RC_OK) {
            return rc;  /* propagate any error from appendEmptyBlock */
        }
    }
    return RC_OK;
}

/*
 * seekToPageNum (internal helper)
 *
 * Move the underlying FILE* pointer in fHandle to the byte offset representing
 * the start of page pageNum. Steps:
 *   1. Validate pageNum in [0, totalNumPages).
 *   2. Compute offset = pageNum * PAGE_SIZE_BYTES.
 *   3. fseek(ctx->fp, offset, SEEK_SET).
 *   4. Return RC_OK or RC_READ_NON_EXISTING_PAGE on error.
 */
static RC seekToPageNum(int pageNum, SM_FileHandle *fHandle) {
    if (fHandle == NULL || fHandle->mgmtInfo == NULL) {
        return RC_FILE_HANDLE_NOT_INIT;
    }
    if (pageNum < 0 || pageNum >= fHandle->totalNumPages) {
        return RC_READ_NON_EXISTING_PAGE;
    }

    FileContext *ctx = (FileContext *) fHandle->mgmtInfo;
    long offsetBytes = (long) pageNum * PAGE_SIZE_BYTES;
    if (fseek(ctx->fp, offsetBytes, SEEK_SET) != 0) {
        return RC_READ_NON_EXISTING_PAGE;
    }
    return RC_OK;
}

/*
 * allocateFileContext (internal helper)
 *
 * Allocate and initialize a new FileContext for the given FILE* and fileName.
 * The caller transfers ownership of fileName (must be malloc’d or strdup’d).
 *
 * Returns:
 *   - Pointer to a newly malloc’ed FileContext on success.
 *   - NULL on memory allocation failure.
 *
 * Note: We do NOT copy fileName here; we assume ownership is transferred.
 */
static FileContext* allocateFileContext(const char *fileName, FILE *fp, int totalPages) {
    FileContext *ctx = (FileContext *) malloc(sizeof(FileContext));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->fp    = fp;
    ctx->fname = (char *) fileName;  /* take ownership */
    ctx->pages = totalPages;
    return ctx;
}

/*
 * freeFileContext (internal helper)
 *
 * Close the FILE* in the context and free the memory. Steps:
 *   1. If ctx or ctx->fp is NULL, THROW RC_FILE_HANDLE_NOT_INIT.
 *   2. fclose(ctx->fp).
 *   3. free(ctx) (note: fileName is freed separately in closePageFile).
 *
 * Returns:
 *   - RC_OK on success.
 *   - RC_FILE_HANDLE_NOT_INIT if the context is invalid.
 *   - RC_WRITE_FAILED if fclose fails (rare).
 */
static RC freeFileContext(FileContext *ctx) {
    if (ctx == NULL || ctx->fp == NULL) {
        THROW(RC_FILE_HANDLE_NOT_INIT, "freeFileContext: invalid context or FILE*");
    }
    if (fclose(ctx->fp) != 0) {
        THROW(RC_WRITE_FAILED, "freeFileContext: fclose failed");
    }
    /* We do NOT free ctx->fname here, because the SM_FileHandle is
     * responsible for that. We only free the context struct itself.
     */
    free(ctx);
    return RC_OK;
}
