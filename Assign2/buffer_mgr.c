// Prevent dt.h from redefining bool
#define bool _Bool
#define true 1
#define false 0

#include "buffer_mgr.h"
#include "storage_mgr.h"
#include "dberror.h"
#include "buffer_mgr_stat.h"
#include "dt.h"
#include <stdlib.h>
#include <string.h>

// Frame structure for buffer pool slots
typedef struct Frame {
    PageNumber pageId;
    char *data;
    bool isDirty;
    int pinCount;
    struct Frame *prev, *next; // for LRU list
} Frame;

// Metadata for buffer pool
typedef struct PoolMetadata {
    SM_FileHandle fh;
    Frame *frames;
    int capacity;
    ReplacementStrategy strat;
    unsigned readIO;
    unsigned writeIO;
    int *fifoQ;
    int fifoHead;
    int fifoCount;
    Frame *lruHead;
    Frame *lruTail;
} PoolMetadata;

// Move frame to head of LRU list
static void moveToLRUHead(PoolMetadata *md, Frame *f) {
    if (!f || md->lruHead == f) return;
    if (f->prev) f->prev->next = f->next;
    if (f->next) f->next->prev = f->prev;
    if (md->lruTail == f) md->lruTail = f->prev;
    f->prev = NULL;
    f->next = md->lruHead;
    if (md->lruHead) md->lruHead->prev = f;
    md->lruHead = f;
    if (!md->lruTail) md->lruTail = f;
}

// Select a victim frame using FIFO or LRU
static Frame *selectVictim(PoolMetadata *md) {
    if (md->strat == RS_FIFO) {
        int count = md->fifoCount;
        for (int i = 0; i < count; i++) {
            int idx = md->fifoQ[md->fifoHead];
            md->fifoHead = (md->fifoHead + 1) % md->capacity;
            md->fifoCount--;
            if (md->frames[idx].pinCount == 0)
                return &md->frames[idx];
        }
        return NULL;
    } else {
        Frame *f = md->lruTail;
        while (f && f->pinCount > 0) f = f->prev;
        return f;
    }
}

// Enqueue a frame index for FIFO replacement
static void enqueueFIFO(PoolMetadata *md, int idx) {
    int tail = (md->fifoHead + md->fifoCount) % md->capacity;
    md->fifoQ[tail] = idx;
    md->fifoCount++;
}

// Initialize the buffer pool
RC initBufferPool(BM_BufferPool *bm, const char *pageFileName,
                  int numPages, ReplacementStrategy strat,
                  void *stratData) {
    SM_FileHandle fh;
    RC rc = openPageFile((char *)pageFileName, &fh);
    if (rc == RC_FILE_NOT_FOUND) {
        return RC_FILE_NOT_FOUND;
    }
    CHECK(rc);

    PoolMetadata *md = malloc(sizeof(PoolMetadata));
    md->fh = fh;
    md->capacity = numPages;
    md->strat = strat;
    md->readIO = md->writeIO = 0;
    md->frames = calloc(numPages, sizeof(Frame));
    for (int i = 0; i < numPages; i++) {
        md->frames[i].pageId = NO_PAGE;
        md->frames[i].data = malloc(PAGE_SIZE);
        md->frames[i].isDirty = false;
        md->frames[i].pinCount = 0;
        md->frames[i].prev = md->frames[i].next = NULL;
    }
    md->fifoQ = malloc(sizeof(int) * numPages);
    md->fifoHead = md->fifoCount = 0;
    md->lruHead = md->lruTail = NULL;

    bm->pageFile = strdup(pageFileName);
    bm->numPages = numPages;
    bm->strategy = strat;
    bm->mgmtData = md;
    return RC_OK;
}

// Shutdown the buffer pool
RC shutdownBufferPool(BM_BufferPool *bm) {
    if (!bm || !bm->mgmtData) return RC_FILE_HANDLE_NOT_INIT;
    PoolMetadata *md = bm->mgmtData;
    // flush dirty unpinned
    for (int i = 0; i < md->capacity; i++) {
        Frame *f = &md->frames[i];
        if (f->pageId != NO_PAGE && f->isDirty && f->pinCount == 0) {
            writeBlock(f->pageId, &md->fh, f->data);
            md->writeIO++;
        }
    }
    closePageFile(&md->fh);
    for (int i = 0; i < md->capacity; i++) free(md->frames[i].data);
    free(md->frames);
    free(md->fifoQ);
    free(bm->pageFile);
    free(md);
    bm->mgmtData = NULL;
    bm->pageFile = NULL;
    return RC_OK;
}

// Force write all dirty pages
RC forceFlushPool(BM_BufferPool *bm) {
    if (!bm || !bm->mgmtData) return RC_FILE_HANDLE_NOT_INIT;
    PoolMetadata *md = bm->mgmtData;
    for (int i = 0; i < md->capacity; i++) {
        Frame *f = &md->frames[i];
        if (f->pageId != NO_PAGE && f->isDirty && f->pinCount == 0) {
            writeBlock(f->pageId, &md->fh, f->data);
            md->writeIO++;
            f->isDirty = false;
        }
    }
    return RC_OK;
}

// Pin a page into the buffer pool
RC pinPage(BM_BufferPool *bm, BM_PageHandle *ph, PageNumber pid) {
    if (!bm || !bm->mgmtData) return RC_FILE_HANDLE_NOT_INIT;
    if (pid < 0) return RC_READ_NON_EXISTING_PAGE;
    PoolMetadata *md = bm->mgmtData;
    Frame *slot = NULL;
    int freeIdx = -1;
    // hit check + find free
    for (int i = 0; i < md->capacity; i++) {
        if (md->frames[i].pageId == pid) {
            slot = &md->frames[i];
            slot->pinCount++;
            if (md->strat == RS_LRU || md->strat == RS_LRU_K)
                moveToLRUHead(md, slot);
            ph->pageNum = pid;
            ph->data = slot->data;
            return RC_OK;
        }
        if (md->frames[i].pageId == NO_PAGE && freeIdx < 0)
            freeIdx = i;
    }
    // miss: free slot or victim
    if (freeIdx >= 0) {
        slot = &md->frames[freeIdx];
    } else {
        slot = selectVictim(md);
        if (!slot) return RC_READ_NON_EXISTING_PAGE;
        if (slot->isDirty) {
            writeBlock(slot->pageId, &md->fh, slot->data);
            md->writeIO++;
        }
    }
    if (pid >= md->fh.totalNumPages) ensureCapacity(pid + 1, &md->fh);
    readBlock(pid, &md->fh, slot->data);
    md->readIO++;
    slot->pageId = pid;
    slot->isDirty = false;
    slot->pinCount = 1;
    int idx = slot - md->frames;
    if (md->strat == RS_FIFO) enqueueFIFO(md, idx);
    else moveToLRUHead(md, slot);
    ph->pageNum = pid;
    ph->data = slot->data;
    return RC_OK;
}

// Unpin a page
RC unpinPage(BM_BufferPool *bm, BM_PageHandle *ph) {
    if (!bm || !bm->mgmtData) return RC_FILE_HANDLE_NOT_INIT;
    PoolMetadata *md = bm->mgmtData;
    for (int i = 0; i < md->capacity; i++) {
        if (md->frames[i].pageId == ph->pageNum) {
            if (md->frames[i].pinCount > 0) {
                md->frames[i].pinCount--;
                return RC_OK;
            } else {
                return RC_READ_NON_EXISTING_PAGE;
            }
        }
    }
    return RC_READ_NON_EXISTING_PAGE;
}

// Mark a page dirty
RC markDirty(BM_BufferPool *bm, BM_PageHandle *ph) {
    if (!bm || !bm->mgmtData) return RC_FILE_HANDLE_NOT_INIT;
    PoolMetadata *md = bm->mgmtData;
    // search for frame
    for (int i = 0; i < md->capacity; i++) {
        if (md->frames[i].pageId == ph->pageNum) {
            md->frames[i].isDirty = true;
            return RC_OK;
        }
    }
    // page not in buffer
    return RC_READ_NON_EXISTING_PAGE;
}

// Force a single page write
RC forcePage(BM_BufferPool *bm, BM_PageHandle *ph) {
    if (!bm || !bm->mgmtData) return RC_FILE_HANDLE_NOT_INIT;
    PoolMetadata *md = bm->mgmtData;
    for (int i = 0; i < md->capacity; i++) {
        if (md->frames[i].pageId == ph->pageNum) {
            writeBlock(ph->pageNum, &md->fh, md->frames[i].data);
            md->writeIO++;
            md->frames[i].isDirty = false;
            return RC_OK;
        }
    }
    return RC_READ_NON_EXISTING_PAGE;
}

// Statistics APIs
PageNumber *getFrameContents(BM_BufferPool *bm) {
    PoolMetadata *md = bm->mgmtData;
    PageNumber *arr = malloc(sizeof(PageNumber) * md->capacity);
    for (int i = 0; i < md->capacity; i++) arr[i] = md->frames[i].pageId;
    return arr;
}
bool *getDirtyFlags(BM_BufferPool *bm) {
    PoolMetadata *md = bm->mgmtData;
    bool *flags = malloc(sizeof(bool) * md->capacity);
    for (int i = 0; i < md->capacity; i++) flags[i] = md->frames[i].isDirty;
    return flags;
}
int *getFixCounts(BM_BufferPool *bm) {
    PoolMetadata *md = bm->mgmtData;
    int *cnt = malloc(sizeof(int) * md->capacity);
    for (int i = 0; i < md->capacity; i++) cnt[i] = md->frames[i].pinCount;
    return cnt;
}
int getNumReadIO(BM_BufferPool *bm) { return ((PoolMetadata *)bm->mgmtData)->readIO; }
int getNumWriteIO(BM_BufferPool *bm) { return ((PoolMetadata *)bm->mgmtData)->writeIO; }
