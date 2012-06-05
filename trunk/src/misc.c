/*
  misc.c :  miscellaneous portability support

  ====================================================================
    Copyright 2010 Greg Stein

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
  ====================================================================
*/

#include <string.h>

#include "pc_types.h"
#include "pc_misc.h"
#include "pc_memory.h"

#include "pocore.h"


pc_context_t *pc_context_create(void)
{
    return pc_context_create_custom(PC_DEFAULT_STDSIZE, NULL, TRUE);
}


pc_context_t *pc_context_create_custom(size_t stdsize,
                                       int (*oom_handler)(size_t amt),
                                       pc_bool_t track_unhandled)
{
    pc_context_t *ctx;

#ifdef PC__IS_WINDOWS
    {
        HANDLE heap;

        heap = HeapCreate(HEAP_NO_SERIALIZE,
                          0 /* dwInitialSize */,
                          0 /* dwMaximumSize */);
        ctx = HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(*ctx));
        ctx->heap = heap;
    }
#else
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
#endif

    if (stdsize == PC_DEFAULT_STDSIZE)
        stdsize = PC_MEMBLOCK_SIZE;
    else if (stdsize < PC_MEMBLOCK_MINIMUM)
        stdsize = PC_MEMBLOCK_MINIMUM;

    ctx->oom_handler = oom_handler;
    ctx->stdsize = stdsize;
    ctx->track_unhandled = track_unhandled;

    return ctx;
}


void pc_context_destroy(pc_context_t *ctx)
{
    /* ### do something about unhandled errors?  */

    if (ctx->cctx != NULL)
        pc__channel_cleanup(ctx);

    /* Blast all memroots. This will destroy CLEANUP_POOL, ERROR_POOL,
       and anything the channel subsystem may have allocated.

       Note: we destroy the head of the list, which is faster for
       pc_pool_destroy() to pop the memroot from the list.

       ### right now, pc__channel_cleanup() preemptively destroys its
       ### private pool. that may continue to make sense if, say, we
       ### provide a way to shut down the channel system, so it should
       ### continue to manage its internal pool.  */
    while (ctx->memroots != NULL)
        pc_pool_destroy(ctx->memroots->pool);

    /* ### it would probably be best to just dig into the tree structure
       ### rather than pop one out at a time, with the associated tree
       ### rebalancing. ie. switch from O(N log N) to just O(N).  */
    while (ctx->nonstd_blocks != NULL)
    {
        struct pc_block_s *scan;

        /* Keep fetching the smallest node possible until it runs out.  */
        scan = pc__memtree_fetch(&ctx->nonstd_blocks,
                                 sizeof(struct pc_memtree_s));
        PC__FREE(ctx, scan);
    }

#ifdef PC__IS_WINDOWS
    HeapDestroy(ctx->heap);
#else
    free(ctx);
#endif
}


void pc_context_tracing(pc_context_t *ctx, pc_bool_t tracing)
{
    ctx->tracing = tracing;
}


pc_error_t *pc_context_unhandled(pc_context_t *ctx)
{
    if (ctx->unhandled == NULL)
        return NULL;
    return &ctx->unhandled->error;
}


void pc__context_init_mutex(pc_context_t *ctx)
{
    /* ### todo. make sure to use swapptr to avoid rewriting non-NULL.  */
}


void pc_lib_version(
    int *major,
    int *minor,
    int *patch)
{
    *major = PC_MAJOR_VERSION;
    *minor = PC_MINOR_VERSION;
    *patch = PC_PATCH_VERSION;
}
