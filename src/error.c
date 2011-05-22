/*
  error.c :  PoCore error handling

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
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>

#include "pc_types.h"
#include "pc_memory.h"
#include "pc_error.h"

#include "pocore.h"


/* This marker is used to stop the "unhandled" processing of freeing
   errors. The marker is placed into the PREVIOUS field of the
   pc_error_list_s structure. We do not want to free this error (or its
   children) because the error is wrapped by multiple (unknown) parents.
   We can't detach the error from the parents, so the best we can do is
   just avoid multiple free's.  */
static const int marker;
#define STOP_PROCESSING_MARKER ((struct pc_error_list_s *)&marker)


/* Is LINK somewhere on the UNHANDLED list?  */
#define LINK_ON_UNHANDLED(link) \
    (((link)->previous != NULL                       \
      && (link)->previous != STOP_PROCESSING_MARKER) \
     || (link)->next != NULL                         \
     || (link)->error.ctx->unhandled == (link))


static pc_error_t *
scan_useful(const pc_error_t *error)
{
    while (error != NULL && error->code == PC_ERR_TRACE)
        error = error->original;

    /* Most callers need to manipulate the error. Lose the const.  */
    return (pc_error_t *)error;
}


static pc_error_t *
create_error(pc_context_t *ctx,
             int code,
             const char *msg,
             const char *file,
             int lineno,
             pc_error_t *original)
{
    pc_error_t *error;

    /* ### does the error pool require any special configuration?  */
    if (ctx->error_pool == NULL)
        ctx->error_pool = pc_pool_root(ctx);

    if (ctx->track_unhandled)
    {
        struct pc_error_list_s *link;

        /* Allocate the linking structure and link it into the head of
           the UNHANDLED list in the context.  */
        link = pc_alloc(ctx->error_pool, sizeof(*link));
        link->previous = NULL;
        link->next = ctx->unhandled;
        ctx->unhandled = link;

        /* Reference the embedded error structure.  */
        error = &link->error;
    }
    else
    {
        error = pc_alloc(ctx->error_pool, sizeof(*error));
    }

    error->ctx = ctx;
    error->code = code;
    error->msg = msg ? pc_strdup(ctx->error_pool, msg) : NULL;
    error->file = file;
    error->lineno = lineno;
    error->original = original;
    error->separate = NULL;

    return error;
}


static void free_error(pc_error_t *error)
{
    if (error->original != NULL)
        free_error(error->original);
    if (error->separate != NULL)
        free_error(error->separate);

    /* Free the dup'd message. Note we have to cast away the const.  */
    if (error->msg != NULL)
        pc_pool_freemem(error->ctx->error_pool,
                        (void *)error->msg,
                        strlen(error->msg) + 1);

    pc_pool_freemem(error->ctx->error_pool, error, sizeof(*error));
}


static void free_link(struct pc_error_list_s *link)
{
    /* If this error has been marked (due to an improper call to
       pc_error_handled()), then we need to leave it for later
       investigation. Just stop processing.  */
    if (link->previous == STOP_PROCESSING_MARKER)
        return;

    /* If we're free'ing this, it should not be in the UNHANDLED list.  */
    assert(!LINK_ON_UNHANDLED(link));

    if (link->error.original != NULL)
        free_link((struct pc_error_list_s *)link->error.original);
    if (link->error.separate != NULL)
        free_link((struct pc_error_list_s *)link->error.separate);

    /* Free the dup'd message. Note we have to cast away the const.  */
    if (link->error.msg != NULL)
        pc_pool_freemem(link->error.ctx->error_pool,
                        (void *)link->error.msg,
                        strlen(link->error.msg) + 1);

    pc_pool_freemem(link->error.ctx->error_pool, link, sizeof(*link));
}


void pc_error_handled(pc_error_t *error)
{
    struct pc_error_list_s *link;

    if (!error->ctx->track_unhandled)
    {
        /* Just free the structures and get outta here.  */
        free_error(error);
        return;
    }

    /* If an error exists, then it must be on the UNHANDLED list, or there
       must be a wrapping error on the UNHANDLED list.  */
    assert(error->ctx->unhandled != NULL);

    link = (struct pc_error_list_s *)error;

    /* If this error is not on the UNHANDLED list, then it must be wrapped
       by another error which IS on the UNHANDLED list. Marking this error
       as "handled" is wrong. The wrapping error should be the one that
       gets marked.

       For this situation, we will wrap the error and place it onto the
       UNHANDLED list (so the application can detect the improper call).
       This error will be specially marked so when the "true" wrapping
       error is marked as handled, we will stop the marking process
       (and avoid a double-free of this error from each wrapper).  */
    if (!LINK_ON_UNHANDLED(link))
    {
        /* Mark this error to stop future unhandled-processing.  */
        link->previous = STOP_PROCESSING_MARKER;

        /* The wrapping error will be placed onto the UNHANDLED list.
           We cannot use pc_error_wrap() because of the marker that
           we just placed onto this error.  */
        (void) create_error(error->ctx, PC_ERR_IMPROPER_UNHANDLED_CALL, NULL,
                            __FILE__, __LINE__, error);

        return;
    }

    /* This will toss the entire tree of error (link) structures.  */
    free_link(link);
}


int pc_error_code(const pc_error_t *error)
{
    const pc_error_t *useful = scan_useful(error);

    return useful ? useful->code : PC_SUCCESS;
}


const char *pc_error_message(const pc_error_t *error)
{
    const pc_error_t *useful = scan_useful(error);

    if (useful == NULL)
        return NULL;

    if (useful->msg == NULL)
        return NULL;  /* ### look up default message  */

    return useful->msg;
}


pc_error_t *pc_error_original(const pc_error_t *error)
{
    return scan_useful(error);
}


pc_error_t *pc_error_separate(const pc_error_t *error)
{
    pc_error_t *scan = scan_useful(error);

    /* Woah. There should have been a useful error. Oh well...  */
    if (scan == NULL)
        return NULL;

    /* Return the first non-tracing error found. Note that scan_useful()
       can be passed a NULL, so no check on SEPARATE is needed.  */
    return scan_useful(scan->separate);
}


void pc_error_trace_info(const char **file,
                         int *lineno,
                         const pc_error_t **original,
                         const pc_error_t **separate,
                         const pc_error_t *error)
{
    *file = error->file;
    *lineno = error->lineno;
    *original = error->original;
    *separate = error->separate;
}


static void
unlink_wrapped(pc_error_t *error, const char *file, int lineno)
{
    struct pc_error_list_s *link = (struct pc_error_list_s *)error;

    /* The error that is being wrapped should be in the UNHANDLED list
       already. If it is NOT in the list, then the caller has attempted
       to wrap this error multiple times. We will wrap this error,
       placing the result into the UNHANDLED list for later processing.
       The error is also marked to avoid stop the unhandled process, to
       avoid multiple free's.  */
    if (!LINK_ON_UNHANDLED(link))
    {
        /* Mark this error to stop future unhandled-processing.  */
        link->previous = STOP_PROCESSING_MARKER;

        /* The wrapping error will be placed onto the UNHANDLED list.
           We cannot use pc_error_wrap() because of the marker that
           we just placed onto this error (creating a recursion back
           to here).  */
        (void) create_error(error->ctx, PC_ERR_IMPROPER_WRAP, NULL,
                            file, lineno, error);

        return;
    }

    /* If the error is the HEAD of the UNHANDLED list, then do some
       special processing.  */
    if (link->error.ctx->unhandled == link)
    {
        assert(link->previous == NULL);

        link->error.ctx->unhandled = link->next;
        if (link->next != NULL)
            link->next->previous = NULL;
        link->next = NULL;
    }
    else
    {
        assert(link->previous != NULL);

        link->previous->next = link->next;
        if (link->next != NULL)
            link->next->previous = link->previous;
        link->previous = NULL;
        link->next = NULL;
    }
}


/* Internal constructors. Use pc_error_create() and friends, instead.  */
pc_error_t *pc__error_create_internal(pc_context_t *ctx,
                                      int code,
                                      const char *msg,
                                      const char *file,
                                      int lineno)
{
    return create_error(ctx, code, msg, file, lineno, NULL);
}


pc_error_t *pc__error_create_internal_via(pc_pool_t *pool,
                                          int code,
                                          const char *msg,
                                          const char *file,
                                          int lineno)
{
    return create_error(pool->ctx, code, msg, file, lineno, NULL);
}


pc_error_t *pc__error_createf_internal(pc_context_t *ctx,
                                       int code,
                                       const char *format,
                                       const char *file,
                                       int lineno,
                                       ...)
{
    pc_error_t *error;
    va_list ap;

    error = create_error(ctx, code, NULL, file, lineno, NULL);

    va_start(ap, lineno);
    error->msg = pc_vsprintf(ctx->error_pool, format, ap);
    va_end(ap);

    return error;
}


pc_error_t *pc__error_createf_internal_via(pc_pool_t *pool,
                                           int code,
                                           const char *format,
                                           const char *file,
                                           int lineno,
                                           ...)
{
    pc_error_t *error;
    va_list ap;

    error = create_error(pool->ctx, code, NULL, file, lineno, NULL);

    va_start(ap, lineno);
    error->msg = pc_vsprintf(pool->ctx->error_pool, format, ap);
    va_end(ap);

    return error;
}


pc_error_t *pc__error_wrap_internal(int code,
                                    const char *msg,
                                    pc_error_t *original,
                                    const char *file,
                                    int lineno)
{
    unlink_wrapped(original, file, lineno);

    return create_error(original->ctx, code, msg, file, lineno, original);
}


pc_error_t *pc__error_join_internal(pc_error_t *error,
                                    pc_error_t *separate,
                                    const char *file,
                                    int lineno)
{
    pc_error_t *scan;

    unlink_wrapped(separate, file, lineno);

    /* Hook SEPARATE onto the end of the chain of ERROR's SEPARATE errors.  */
    scan = scan_useful(error);
    while (scan->separate != NULL)
        scan = error->separate;
    scan->separate = separate;

    /* Wrap a trace record to annotate where the join happened.  */
    return pc__error_trace_internal(error, file, lineno);
}


pc_error_t *pc__error_trace_internal(pc_error_t *error,
                                     const char *file,
                                     int lineno)
{
    if (error && error->ctx->tracing)
        return create_error(error->ctx, PC_ERR_TRACE, NULL, file, lineno,
                            error);
    return error;
}