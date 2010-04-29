/*
  misc.c :  miscellaneous portability support

  ====================================================================
     Licensed to the Apache Software Foundation (ASF) under one
     or more contributor license agreements.  See the NOTICE file
     distributed with this work for additional information
     regarding copyright ownership.  The ASF licenses this file
     to you under the Apache License, Version 2.0 (the
     "License"); you may not use this file except in compliance
     with the License.  You may obtain a copy of the License at
    
       http://www.apache.org/licenses/LICENSE-2.0
    
     Unless required by applicable law or agreed to in writing,
     software distributed under the License is distributed on an
     "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
     KIND, either express or implied.  See the License for the
     specific language governing permissions and limitations
     under the License.
  ====================================================================
*/

#include <string.h>

#include "pc_types.h"
#include "pc_misc.h"

#include "pocore.h"


pc_context_t *pc_context_create(size_t stdsize,
                                int (*oom_handler)(size_t amt))
{
    pc_context_t *ctx = malloc(sizeof(*ctx));

    memset(ctx, 0, sizeof(*ctx));

    ctx->oom_handler = oom_handler;
    ctx->stdsize = stdsize;

    return ctx;
}


void pc_context_destroy(pc_context_t *ctx)
{
    /* ### do stuff with the memory hanging around in here.  */

    free(ctx);
}
