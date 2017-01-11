#include "trace.h"
#include "gc.h"
#include "../sched/scheduler.h"
#include "../sched/cpu.h"
#include "../actor/actor.h"
#include <assert.h>
#include <dtrace.h>
#include <stdio.h>

void pony_gc_send(pony_ctx_t* ctx)
{
  assert(ctx->stack == NULL);
  ctx->trace_object = ponyint_gc_sendobject;
  ctx->trace_actor = ponyint_gc_sendactor;

  if(is_source(ctx->current))
  {
    ctx->current->send_count++;
    ctx->current->send_time_start_ts = ponyint_cpu_tick();
  }
}

void pony_gc_recv(pony_ctx_t* ctx)
{
  assert(ctx->stack == NULL);
  ctx->trace_object = ponyint_gc_recvobject;
  ctx->trace_actor = ponyint_gc_recvactor;

  DTRACE1(GC_RECV_START, (uintptr_t)ctx->scheduler);
}

void ponyint_gc_mark(pony_ctx_t* ctx)
{
  assert(ctx->stack == NULL);
  ctx->trace_object = ponyint_gc_markobject;
  ctx->trace_actor = ponyint_gc_markactor;
}

void pony_gc_acquire(pony_ctx_t* ctx)
{
  assert(ctx->stack == NULL);
  ctx->trace_object = ponyint_gc_acquireobject;
  ctx->trace_actor = ponyint_gc_acquireactor;
}

void pony_gc_release(pony_ctx_t* ctx)
{
  assert(ctx->stack == NULL);
  ctx->trace_object = ponyint_gc_releaseobject;
  ctx->trace_actor = ponyint_gc_releaseactor;
}

void pony_send_done(pony_ctx_t* ctx)
{
  uint64_t tsc = ponyint_cpu_tick();
  ponyint_gc_handlestack(ctx);
  uint64_t tsc2 = ponyint_cpu_tick();
  ponyint_gc_sendacquire(ctx);
  uint64_t tsc3 = ponyint_cpu_tick();
  ponyint_gc_done(ponyint_actor_gc(ctx->current));

  if(is_source(ctx->current))
  {
    ctx->current->send_time += tsc3 - ctx->current->send_time_start_ts;
    ctx->current->send_time_bhs += tsc - ctx->current->send_time_start_ts;
    ctx->current->send_time_hs += tsc2 - tsc;
    ctx->current->send_time_aq += tsc3 - tsc2;
    if(ctx->current->send_count == 2000000)
    {
      printf("tcp source send count: %u. send_time: %lu. send_time_bhs: %lu. send_time_hs: %lu. send_time_aq: %lu. obj time: %lu. obj count: %u. obj size: %lu, obj count: %lu\n", ctx->current->send_count, ctx->current->send_time/ctx->current->send_count, ctx->current->send_time_bhs/ctx->current->send_count, ctx->current->send_time_hs/ctx->current->send_count, ctx->current->send_time_aq/ctx->current->send_count, ctx->current->obj_time/ctx->current->obj_count, ctx->current->obj_count, ctx->current->gc.local.contents.size, ctx->current->gc.local.contents.count);

      ctx->current->send_time = 0;
      ctx->current->send_time_bhs = 0;
      ctx->current->send_time_hs = 0;
      ctx->current->send_time_aq = 0;
      ctx->current->send_count = 0;
      ctx->current->obj_time = 0;
      ctx->current->obj_count = 0;
    }
  }
}

void pony_recv_done(pony_ctx_t* ctx)
{
  ponyint_gc_handlestack(ctx);
  ponyint_gc_done(ponyint_actor_gc(ctx->current));

  DTRACE1(GC_RECV_END, (uintptr_t)ctx->scheduler);
}

void ponyint_mark_done(pony_ctx_t* ctx)
{
  ponyint_gc_markimmutable(ctx, ponyint_actor_gc(ctx->current));
  ponyint_gc_handlestack(ctx);
  ponyint_gc_sendacquire(ctx);
  ponyint_gc_sweep(ctx, ponyint_actor_gc(ctx->current));
  ponyint_gc_done(ponyint_actor_gc(ctx->current));
}

void pony_acquire_done(pony_ctx_t* ctx)
{
  ponyint_gc_handlestack(ctx);
  ponyint_gc_sendacquire(ctx);
  ponyint_gc_done(ponyint_actor_gc(ctx->current));
}

void pony_release_done(pony_ctx_t* ctx)
{
  ponyint_gc_handlestack(ctx);
  ponyint_gc_sendrelease_manual(ctx);
  ponyint_gc_done(ponyint_actor_gc(ctx->current));
}

void pony_send_next(pony_ctx_t* ctx)
{
  ponyint_gc_handlestack(ctx);
  ponyint_gc_done(ponyint_actor_gc(ctx->current));
}

void pony_trace(pony_ctx_t* ctx, void* p)
{
  ctx->trace_object(ctx, p, NULL, PONY_TRACE_OPAQUE);
}

void pony_traceknown(pony_ctx_t* ctx, void* p, pony_type_t* t, int m)
{
  if(t->dispatch != NULL)
  {
    ctx->trace_actor(ctx, (pony_actor_t*)p);
  } else {
    ctx->trace_object(ctx, p, t, m);
  }
}

void pony_traceunknown(pony_ctx_t* ctx, void* p, int m)
{
  pony_type_t* t = *(pony_type_t**)p;

  if(t->dispatch != NULL)
  {
    ctx->trace_actor(ctx, (pony_actor_t*)p);
  } else {
    ctx->trace_object(ctx, p, t, m);
  }
}
