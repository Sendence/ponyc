#include "actor.h"
#include "../sched/cpu.h"
#include "../mem/pool.h"
#include "../gc/cycle.h"
#include "../gc/trace.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

enum
{
  FLAG_BLOCKED = 1 << 0,
  FLAG_RC_CHANGED = 1 << 1,
  FLAG_SYSTEM = 1 << 2,
  FLAG_UNSCHEDULED = 1 << 3,
  FLAG_BACKPRESSURE = 1 << 4,
  FLAG_PENDINGDESTROY = 1 << 5,
};

static bool actor_noblock = false;

static bool has_flag(pony_actor_t* actor, uint8_t flag)
{
  return (actor->flags & flag) != 0;
}

static void set_flag(pony_actor_t* actor, uint8_t flag)
{
  actor->flags |= flag;
}

static void unset_flag(pony_actor_t* actor, uint8_t flag)
{
  actor->flags &= (uint8_t)~flag;
}

static void try_gc(pony_ctx_t* ctx, pony_actor_t* actor)
{
  if(!ponyint_heap_startgc(&actor->heap))
    return;

#ifdef USE_TELEMETRY
  ctx->count_gc_passes++;
  size_t tsc = ponyint_cpu_tick();
#endif

  ponyint_gc_mark(ctx);

  if(actor->type->trace != NULL)
    actor->type->trace(ctx, actor);

  ponyint_mark_done(ctx);
  ponyint_heap_endgc(&actor->heap);

#ifdef USE_TELEMETRY
  ctx->time_in_gc += (ponyint_cpu_tick() - tsc);
#endif
}

static bool handle_message(pony_ctx_t* ctx, pony_actor_t* actor,
  pony_msg_t* msg)
{
  switch(msg->id)
  {
    case ACTORMSG_ACQUIRE:
    {
      pony_msgp_t* m = (pony_msgp_t*)msg;

      if(ponyint_gc_acquire(&actor->gc, (actorref_t*)m->p) &&
        has_flag(actor, FLAG_BLOCKED))
      {
        // If our rc changes, we have to tell the cycle detector before sending
        // any CONF messages.
        set_flag(actor, FLAG_RC_CHANGED);
      }

      return false;
    }

    case ACTORMSG_RELEASE:
    {
      pony_msgp_t* m = (pony_msgp_t*)msg;

      if(ponyint_gc_release(&actor->gc, (actorref_t*)m->p) &&
        has_flag(actor, FLAG_BLOCKED))
      {
        // If our rc changes, we have to tell the cycle detector before sending
        // any CONF messages.
        set_flag(actor, FLAG_RC_CHANGED);
      }

      return false;
    }

    case ACTORMSG_CONF:
    {
      if(has_flag(actor, FLAG_BLOCKED) && !has_flag(actor, FLAG_RC_CHANGED))
      {
        // We're blocked and our RC hasn't changed since our last block
        // message, send confirm.
        pony_msgi_t* m = (pony_msgi_t*)msg;
        ponyint_cycle_ack(ctx, m->i);
      }

      return false;
    }

    default:
    {
      if(has_flag(actor, FLAG_BLOCKED))
      {
        // Send unblock before continuing. We no longer need to send any
        // pending rc change to the cycle detector.
        unset_flag(actor, FLAG_BLOCKED | FLAG_RC_CHANGED);
        ponyint_cycle_unblock(ctx, actor);
      }

      actor->type->dispatch(ctx, actor, msg);
      try_gc(ctx, actor);
      return true;
    }
  }
}

static bool handle_continuation(pony_ctx_t* ctx, pony_actor_t* actor)
{
  pony_msg_t* msg = actor->continuation;

  if(msg == NULL)
    return false;

  actor->continuation = msg->next;
  bool ret = handle_message(ctx, actor, msg);
  ponyint_pool_free(msg->index, msg);

  // If we handle an application message, try to gc.
  if(ret)
    try_gc(ctx, actor);

  return ret;
}

sched_level_t ponyint_actor_run(pony_ctx_t* ctx, pony_actor_t* actor,
  size_t batch)
{
  ctx->current = actor;
  ctx->loaded_sends = 0;

  pony_msg_t* msg = NULL;
  size_t app = 0;
  bool run = true;

  while(run && (actor->continuation != NULL))
  {
    if(handle_continuation(ctx, actor))
      app++;

    // End this turn if we reach our batch limit, or if we send to a loaded
    // queue.
    run =
      (app < batch) &&
      (ctx->loaded_sends == 0);
  }

  // If we have been scheduled, the head will not be marked as empty.
  pony_msg_t* head = _atomic_load(&actor->q.head);

  while(run && ((msg = ponyint_messageq_pop(&actor->q)) != NULL))
  {
    if(handle_message(ctx, actor, msg))
      app++;

    // End this turn if we reach the head we found when the turn began, or if
    // a continuation was enqueued, or if we reach our batch limit, or if we
    // send to a loaded queue.
    run =
      (msg != head) &&
      (actor->continuation == NULL) &&
      (app < batch) &&
      (ctx->loaded_sends == 0);
  }

  // TODO: if we have a zero RC and an empty queue we can destroy ourself

  // Try GC at the end of the turn.
  try_gc(ctx, actor);

  // Update the loaded queue status.
  bool system = has_flag(actor, FLAG_SYSTEM);

  bool pressure =
    (ctx->loaded_sends > 0) || has_flag(actor, FLAG_BACKPRESSURE);

  bool empty = (msg == NULL) || (msg == head);

  if(actor->loaded_queue)
  {
    // If the queue was previously loaded, mark it unloaded if we handled
    // every message in our queue without sending to a loaded queue.
    actor->loaded_queue = !empty || pressure;
  } else {
    // If the queue was not previously loaded, mark it loaded if we handled a
    // full batch or sent to a loaded queue. Never mark a system actor as
    // having a loaded queue.
    if(!system)
      actor->loaded_queue = (app == batch) || pressure;
  }

  // When unscheduling, don't mark the queue as empty, since we don't want
  // to get rescheduled if we receive a message.
  if(has_flag(actor, FLAG_UNSCHEDULED))
    return SCHED_NONE;

  // If we know we have pending work, don't block.
  if(!ponyint_messageq_maybeempty(&actor->q))
  {
    if(system)
      return SCHED_4;

    return pressure ? SCHED_3 : SCHED_2;
  }

  // Tell the cycle detector we are blocking. We may not actually block if a
  // message is received between now and when we try to mark our queue as
  // empty, but that's ok, we have still logically blocked.
  if(!actor_noblock)
  {
    if(!has_flag(actor, FLAG_BLOCKED | FLAG_SYSTEM) ||
      has_flag(actor, FLAG_RC_CHANGED))
    {
      set_flag(actor, FLAG_BLOCKED);
      unset_flag(actor, FLAG_RC_CHANGED);
      ponyint_cycle_block(ctx, actor, &actor->gc);
    }
  }

  if(ponyint_messageq_markempty(&actor->q))
    return SCHED_NONE;

  if(system)
    return SCHED_4;

  return pressure ? SCHED_3 : SCHED_2;
}

void ponyint_actor_destroy(pony_actor_t* actor)
{
  assert(has_flag(actor, FLAG_PENDINGDESTROY));

  // Make sure the actor being destroyed has finished marking its queue
  // as empty. Otherwise, it may spuriously see that tail and head are not
  // the same and fail to mark the queue as empty, resulting in it getting
  // rescheduled.
  pony_msg_t* head = _atomic_load(&actor->q.head);

  while(((uintptr_t)head & (uintptr_t)1) != (uintptr_t)1)
    head = _atomic_load(&actor->q.head);

  ponyint_messageq_destroy(&actor->q);
  ponyint_gc_destroy(&actor->gc);
  ponyint_heap_destroy(&actor->heap);

  // Free variable sized actors correctly.
  ponyint_pool_free_size(actor->type->size, actor);
}

gc_t* ponyint_actor_gc(pony_actor_t* actor)
{
  return &actor->gc;
}

heap_t* ponyint_actor_heap(pony_actor_t* actor)
{
  return &actor->heap;
}

bool ponyint_actor_pendingdestroy(pony_actor_t* actor)
{
  return has_flag(actor, FLAG_PENDINGDESTROY);
}

void ponyint_actor_setpendingdestroy(pony_actor_t* actor)
{
  set_flag(actor, FLAG_PENDINGDESTROY);
}

void ponyint_actor_final(pony_ctx_t* ctx, pony_actor_t* actor)
{
  // This gets run while the cycle detector is handling a message. Set the
  // current actor before running anything.
  pony_actor_t* prev = ctx->current;
  ctx->current = actor;

  // Run the actor finaliser if it has one.
  if(actor->type->final != NULL)
    actor->type->final(actor);

  // Run all outstanding object finalisers.
  ponyint_gc_final(ctx, &actor->gc);

  // Restore the current actor.
  ctx->current = prev;
}

void ponyint_actor_sendrelease(pony_ctx_t* ctx, pony_actor_t* actor)
{
  ponyint_gc_sendrelease(ctx, &actor->gc);
}

void ponyint_actor_setsystem(pony_actor_t* actor)
{
  set_flag(actor, FLAG_SYSTEM);
}

void ponyint_actor_setnoblock(bool state)
{
  actor_noblock = state;
}

pony_actor_t* pony_create(pony_ctx_t* ctx, pony_type_t* type)
{
  assert(type != NULL);

#ifdef USE_TELEMETRY
  ctx->count_alloc_actors++;
#endif

  // allocate variable sized actors correctly
  pony_actor_t* actor = (pony_actor_t*)ponyint_pool_alloc_size(type->size);
  memset(actor, 0, type->size);
  actor->type = type;

  ponyint_messageq_init(&actor->q);
  ponyint_heap_init(&actor->heap);
  ponyint_gc_done(&actor->gc);

  if(ctx->current != NULL)
  {
    // actors begin unblocked and referenced by the creating actor
    actor->gc.rc = GC_INC_MORE;
    ponyint_gc_createactor(ctx->current, actor);
  } else {
    // no creator, so the actor isn't referenced by anything
    actor->gc.rc = 0;
  }

  return actor;
}

void ponyint_destroy(pony_actor_t* actor)
{
  // This destroy an actor immediately. If any other actor has a reference to
  // this actor, the program will likely crash. The finaliser is not called.
  ponyint_actor_setpendingdestroy(actor);
  ponyint_actor_destroy(actor);
}

pony_msg_t* pony_alloc_msg(uint32_t index, uint32_t id)
{
  pony_msg_t* msg = (pony_msg_t*)ponyint_pool_alloc(index);
  msg->index = index;
  msg->id = id;

  return msg;
}

pony_msg_t* pony_alloc_msg_size(size_t size, uint32_t id)
{
  return pony_alloc_msg((uint32_t)ponyint_pool_index(size), id);
}

void pony_sendv(pony_ctx_t* ctx, pony_actor_t* to, pony_msg_t* m)
{
#ifdef USE_TELEMETRY
  switch(m->id)
  {
    case ACTORMSG_BLOCK: ctx->count_msg_block++; break;
    case ACTORMSG_UNBLOCK: ctx->count_msg_unblock++; break;
    case ACTORMSG_ACQUIRE: ctx->count_msg_acquire++; break;
    case ACTORMSG_RELEASE: ctx->count_msg_release++; break;
    case ACTORMSG_CONF: ctx->count_msg_conf++; break;
    case ACTORMSG_ACK: ctx->count_msg_ack++; break;
    default: ctx->count_msg_app++;
  }
#endif

  if(ponyint_messageq_push(&to->q, m))
  {
    if(!has_flag(to, FLAG_UNSCHEDULED))
      ponyint_sched_add(ctx, to);
  } else {
    if(to->loaded_queue)
      ctx->loaded_sends++;
  }
}

void pony_send(pony_ctx_t* ctx, pony_actor_t* to, uint32_t id)
{
  pony_msg_t* m = pony_alloc_msg(POOL_INDEX(sizeof(pony_msg_t)), id);
  pony_sendv(ctx, to, m);
}

void pony_sendp(pony_ctx_t* ctx, pony_actor_t* to, uint32_t id, void* p)
{
  pony_msgp_t* m = (pony_msgp_t*)pony_alloc_msg(
    POOL_INDEX(sizeof(pony_msgp_t)), id);
  m->p = p;

  pony_sendv(ctx, to, &m->msg);
}

void pony_sendi(pony_ctx_t* ctx, pony_actor_t* to, uint32_t id, intptr_t i)
{
  pony_msgi_t* m = (pony_msgi_t*)pony_alloc_msg(
    POOL_INDEX(sizeof(pony_msgi_t)), id);
  m->i = i;

  pony_sendv(ctx, to, &m->msg);
}

void pony_continuation(pony_actor_t* to, pony_msg_t* m)
{
  m->next = to->continuation;
  to->continuation = m;
}

void* pony_alloc(pony_ctx_t* ctx, size_t size)
{
#ifdef USE_TELEMETRY
  ctx->count_alloc++;
  ctx->count_alloc_size += size;
#endif

  return ponyint_heap_alloc(ctx->current, &ctx->current->heap, size);
}

void* pony_alloc_small(pony_ctx_t* ctx, uint32_t sizeclass)
{
#ifdef USE_TELEMETRY
  ctx->count_alloc++;
  ctx->count_alloc_size += HEAP_MIN << sizeclass;
#endif

  return ponyint_heap_alloc_small(ctx->current, &ctx->current->heap,
    sizeclass);
}

void* pony_alloc_large(pony_ctx_t* ctx, size_t size)
{
#ifdef USE_TELEMETRY
  ctx->count_alloc++;
  ctx->count_alloc_size += size;
#endif

  return ponyint_heap_alloc_large(ctx->current, &ctx->current->heap, size);
}

void* pony_realloc(pony_ctx_t* ctx, void* p, size_t size)
{
#ifdef USE_TELEMETRY
  ctx->count_alloc++;
  ctx->count_alloc_size += size;
#endif

  return ponyint_heap_realloc(ctx->current, &ctx->current->heap, p, size);
}

void* pony_alloc_final(pony_ctx_t* ctx, size_t size, pony_final_fn final)
{
#ifdef USE_TELEMETRY
  ctx->count_alloc++;
  ctx->count_alloc_size += size;
#endif

  void* p = ponyint_heap_alloc(ctx->current, &ctx->current->heap, size);
  ponyint_gc_register_final(ctx, p, final);
  return p;
}

void pony_triggergc(pony_actor_t* actor)
{
  actor->heap.next_gc = 0;
}

void pony_setbackpressure(pony_actor_t* actor)
{
  // Set the backpressure flag.
  set_flag(actor, FLAG_BACKPRESSURE);
  actor->loaded_queue = true;
}

void pony_unsetbackpressure(pony_actor_t* actor)
{
  // Unset the backpressure flag. The queue may be marked as unloaded in
  // ponyint_actor_run.
  unset_flag(actor, FLAG_BACKPRESSURE);
}

void pony_schedule(pony_ctx_t* ctx, pony_actor_t* actor)
{
  if(!has_flag(actor, FLAG_UNSCHEDULED))
    return;

  unset_flag(actor, FLAG_UNSCHEDULED);
  ponyint_sched_add(ctx, actor);
}

void pony_unschedule(pony_ctx_t* ctx, pony_actor_t* actor)
{
  if(has_flag(actor, FLAG_BLOCKED))
  {
    ponyint_cycle_unblock(ctx, actor);
    unset_flag(actor, FLAG_BLOCKED | FLAG_RC_CHANGED);
  }

  set_flag(actor, FLAG_UNSCHEDULED);
}

void pony_become(pony_ctx_t* ctx, pony_actor_t* actor)
{
  ctx->current = actor;
}

void pony_poll(pony_ctx_t* ctx)
{
  assert(ctx->current != NULL);
  ponyint_actor_run(ctx, ctx->current, 1);
}
