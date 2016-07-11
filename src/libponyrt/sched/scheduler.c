#include "scheduler.h"
#include "cpu.h"
#include "mpmcq.h"
#include "../actor/actor.h"
#include "../gc/cycle.h"
#include "../asio/asio.h"
#include "../mem/pool.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

static DECLARE_THREAD_FN(run_thread);

typedef enum
{
  SCHED_BLOCK,
  SCHED_UNBLOCK,
  SCHED_CNF,
  SCHED_ACK,
  SCHED_TERMINATE
} sched_msg_t;

// Scheduler global data.
static uint32_t scheduler_count;
static scheduler_t* scheduler;
static bool volatile detect_quiescence;
static bool use_yield;
static size_t batch_size;
static mpmcq_t inject;
static __pony_thread_local scheduler_t* this_scheduler;

/**
 * Handles the global queue and then pops from the local active queue
 */
static pony_actor_t* pop_global(scheduler_t* sched)
{
  pony_actor_t* actor = (pony_actor_t*)ponyint_mpmcq_pop(&inject);

  if(actor != NULL)
    return actor;

  return (pony_actor_t*)ponyint_mpmcq_pop(sched->active_q);
}

/**
 * Sends a message to a thread.
 */
static void send_msg(uint32_t to, sched_msg_t msg, intptr_t arg)
{
  pony_msgi_t* m = (pony_msgi_t*)pony_alloc_msg(
    POOL_INDEX(sizeof(pony_msgi_t)), msg);

  m->i = arg;
  ponyint_messageq_push(&scheduler[to].mq, &m->msg);
}

static void read_msg(scheduler_t* sched)
{
  pony_msgi_t* m;

  while((m = (pony_msgi_t*)ponyint_messageq_pop(&sched->mq)) != NULL)
  {
    switch(m->msg.id)
    {
      case SCHED_BLOCK:
      {
        sched->block_count++;

        if(detect_quiescence && (sched->block_count == scheduler_count))
        {
          // If we think all threads are blocked, send CNF(token) to everyone.
          for(uint32_t i = 0; i < scheduler_count; i++)
            send_msg(i, SCHED_CNF, sched->ack_token);
        }
        break;
      }

      case SCHED_UNBLOCK:
      {
        // Cancel all acks and increment the ack token, so that any pending
        // acks in the queue will be dropped when they are received.
        sched->block_count--;
        sched->ack_token++;
        sched->ack_count = 0;
        break;
      }

      case SCHED_CNF:
      {
        // Echo the token back as ACK(token).
        send_msg(0, SCHED_ACK, m->i);
        break;
      }

      case SCHED_ACK:
      {
        // If it's the current token, increment the ack count.
        if(m->i == sched->ack_token)
          sched->ack_count++;
        break;
      }

      case SCHED_TERMINATE:
      {
        sched->terminate = true;
        break;
      }

      default: {}
    }
  }
}

/**
 * If we can terminate, return true. If all schedulers are waiting, one of
 * them will stop the ASIO back end and tell the cycle detector to try to
 * terminate.
 */
static bool quiescent(scheduler_t* sched, uint64_t tsc)
{
  // Look for any additional messages in the queue.
  read_msg(sched);

  if(sched->terminate)
    return true;

  // Check this here because we want to recheck it every time we don't stop the
  // ASIO thread due to possible input but have otherwise reached quiescence.
  if(sched->ack_count == scheduler_count)
  {
    if(sched->asio_stopped)
    {
      for(uint32_t i = 0; i < scheduler_count; i++)
        send_msg(i, SCHED_TERMINATE, 0);

      sched->ack_token++;
      sched->ack_count = 0;
    } else if(ponyint_asio_stop()) {
      sched->asio_stopped = true;
      sched->ack_token++;
      sched->ack_count = 0;

      // Run another CNF/ACK cycle.
      for(uint32_t i = 0; i < scheduler_count; i++)
        send_msg(i, SCHED_CNF, sched->ack_token);
    }
  }

  ponyint_cpu_core_pause(tsc, use_yield);
  return false;
}

static scheduler_t* choose_victim(scheduler_t* sched)
{
  scheduler_t* victim = sched->last_victim;

  while(true)
  {
    // Back up one.
    victim--;

    // Wrap around to the end.
    if(victim < scheduler)
      victim = &scheduler[scheduler_count - 1];

    // Stop looking if we've looked at everything. This only happens with one
    // or two scheduler threads, where the victim is always the same.
    if(victim == sched->last_victim)
      break;

    // Don't try to steal from ourself. If there's only one scheduler thread,
    // we will steal from ourself, and it will always fail, which is fine.
    if(victim != sched)
    {
      // Record that this is our victim and return it.
      sched->last_victim = victim;
      break;
    }
  }

  return victim;
}

/**
 * Use mpmcqs to allow stealing directly from a victim, without waiting for a
 * response.
 */
static pony_actor_t* steal(scheduler_t* sched)
{
  send_msg(0, SCHED_BLOCK, 0);
  uint64_t tsc = ponyint_cpu_tick();

  while(true)
  {
    scheduler_t* victim = choose_victim(sched);
    pony_actor_t* actor = pop_global(victim);

    if(actor != NULL)
    {
      send_msg(0, SCHED_UNBLOCK, 0);
      return actor;
    }

    if(quiescent(sched, tsc))
      break;
  }

  return NULL;
}

static void rotate_queues(scheduler_t* sched)
{
  // Rotate the queues. The active queue (now empty) beomes the pressure
  // queue, the pressure queue becomes the expired queue, and the
  // expired queue becomes the active queue.
  mpmcq_t* pressure_q = sched->active_q;
  mpmcq_t* expired_q = sched->pressure_q;
  mpmcq_t* active_q = sched->expired_q;

  sched->active_q = active_q;
  sched->expired_q = expired_q;
  sched->pressure_q = pressure_q;
}

static pony_actor_t* next_actor(scheduler_t* sched)
{
  // Try the inject queue first. This prioritises actors with new ASIO events.
  pony_actor_t* actor = (pony_actor_t*)ponyint_mpmcq_pop(&inject);

  if(actor != NULL)
    return actor;

  // Try the active queue. These are actors that haven't yet had a turn.
  actor = (pony_actor_t*)ponyint_mpmcq_pop(sched->active_q);

  if(actor != NULL)
    return actor;

  // TODO: could have a no-atomic-op pop for non-active queues

  // Try the expired queue. These are actors that have had a turn. This
  // prioritises actors that have had a turn over actors that have experienced
  // back pressure.
  actor = (pony_actor_t*)ponyint_mpmcq_pop(sched->expired_q);

  if(actor != NULL)
  {
    // By rotating the queues, actors that have been pressured are not left to
    // languish; they become expired actors.
    rotate_queues(sched);
    return actor;
  }

  // Try the pressured actors.
  actor = (pony_actor_t*)ponyint_mpmcq_pop(sched->pressure_q);

  if(actor != NULL)
  {
    // If the cycle detector is the only actor on this scheduler thread, don't
    // run it. Reschedule it and steal from another scheduler thread. This
    // allows the program to terminate when only cycle detection work remains.
    if(ponyint_is_cycle(actor))
    {
      pony_actor_t* alt = (pony_actor_t*)ponyint_mpmcq_pop(sched->pressure_q);
      ponyint_mpmcq_push_single(sched->pressure_q, actor);

      if(alt == NULL)
        return steal(sched);

      actor = alt;
    }

    rotate_queues(sched);
    return actor;
  }

  // No actors are enqueued on this scheduler thread. Either steal an actor
  // from some other scheduler thread, or terminate.
  return steal(sched);
}

/**
 * Run a scheduler thread until termination.
 */
static void run(scheduler_t* sched)
{
  while(true)
  {
    // Handle any scheduler thread messages.
    read_msg(sched);

    // Get an actor to execute.
    pony_actor_t* actor = next_actor(sched);

    // If no actor is available, terminate this scheduler thread.
    if(actor == NULL)
      return;

    // Run and possibly reschedule the actor.
    if(ponyint_actor_run(&sched->ctx, actor, batch_size))
    {
      if(sched->ctx.loaded_sends > 0)
      {
        // If there is back pressure, or it's the cycle detector, schedule the
        // actor on the pressure queue.
        ponyint_mpmcq_push_single(sched->pressure_q, actor);
      } else if(ponyint_is_cycle(actor)) {
        // TODO: move into the previous clause
        ponyint_mpmcq_push_single(sched->pressure_q, actor);
      } else {
        // Otherwise, schedule the actor on the expired queue.
        ponyint_mpmcq_push_single(sched->expired_q, actor);
      }
    }
  }
}

static DECLARE_THREAD_FN(run_thread)
{
  scheduler_t* sched = (scheduler_t*) arg;
  this_scheduler = sched;
  ponyint_cpu_affinity(sched->cpu);
  run(sched);

  return 0;
}

static void ponyint_sched_shutdown()
{
  uint32_t start;

  if(scheduler[0].tid == pony_thread_self())
    start = 1;
  else
    start = 0;

  for(uint32_t i = start; i < scheduler_count; i++)
    pony_thread_join(scheduler[i].tid);

  ponyint_cycle_terminate(&scheduler[0].ctx);

#ifdef USE_TELEMETRY
  printf("\"telemetry\": [\n");
#endif

  for(uint32_t i = 0; i < scheduler_count; i++)
  {
    scheduler_t* sched = &scheduler[i];

    while(ponyint_messageq_pop(&sched->mq) != NULL);
    ponyint_messageq_destroy(&sched->mq);
    ponyint_mpmcq_destroy(&sched->q1);
    ponyint_mpmcq_destroy(&sched->q2);
    ponyint_mpmcq_destroy(&sched->q3);

#ifdef USE_TELEMETRY
    pony_ctx_t* ctx = &sched->ctx;

    printf(
      "  {\n"
      "    \"count_gc_passes\": " __zu ",\n"
      "    \"count_alloc\": " __zu ",\n"
      "    \"count_alloc_size\": " __zu ",\n"
      "    \"count_alloc_actors\": " __zu ",\n"
      "    \"count_msg_app\": " __zu ",\n"
      "    \"count_msg_block\": " __zu ",\n"
      "    \"count_msg_unblock\": " __zu ",\n"
      "    \"count_msg_acquire\": " __zu ",\n"
      "    \"count_msg_release\": " __zu ",\n"
      "    \"count_msg_conf\": " __zu ",\n"
      "    \"count_msg_ack\": " __zu ",\n"
      "    \"time_in_gc\": " __zu ",\n"
      "    \"time_in_send_scan\": " __zu ",\n"
      "    \"time_in_recv_scan\": " __zu "\n"
      "  }",
      ctx->count_gc_passes,
      ctx->count_alloc,
      ctx->count_alloc_size,
      ctx->count_alloc_actors,
      ctx->count_msg_app,
      ctx->count_msg_block,
      ctx->count_msg_unblock,
      ctx->count_msg_acquire,
      ctx->count_msg_release,
      ctx->count_msg_conf,
      ctx->count_msg_ack,
      ctx->time_in_gc,
      ctx->time_in_send_scan,
      ctx->time_in_recv_scan
      );

    if(i < (scheduler_count - 1))
      printf(",\n");
#endif
  }

#ifdef USE_TELEMETRY
  printf("\n]\n");
#endif

  ponyint_pool_free_size(scheduler_count * sizeof(scheduler_t), scheduler);
  scheduler = NULL;
  scheduler_count = 0;

  ponyint_mpmcq_destroy(&inject);
}

pony_ctx_t* ponyint_sched_init(uint32_t threads, bool noyield, size_t batch)
{
  use_yield = !noyield;
  batch_size = batch;

  // If no thread count is specified, use the available physical core count.
  if(threads == 0)
    threads = ponyint_cpu_count();

  scheduler_count = threads;
  scheduler = (scheduler_t*)ponyint_pool_alloc_size(
    scheduler_count * sizeof(scheduler_t));
  memset(scheduler, 0, scheduler_count * sizeof(scheduler_t));

  ponyint_cpu_assign(scheduler_count, scheduler);

  for(uint32_t i = 0; i < scheduler_count; i++)
  {
    scheduler_t* sched = &scheduler[i];

    sched->ctx.scheduler = sched;
    sched->last_victim = sched;
    ponyint_messageq_init(&sched->mq);
    ponyint_mpmcq_init(&sched->q1);
    ponyint_mpmcq_init(&sched->q2);
    ponyint_mpmcq_init(&sched->q3);

    sched->active_q = &sched->q1;
    sched->expired_q = &sched->q2;
    sched->pressure_q = &sched->q3;
  }

  this_scheduler = &scheduler[0];
  ponyint_mpmcq_init(&inject);
  ponyint_asio_init();

  return &scheduler[0].ctx;
}

bool ponyint_sched_start(bool library)
{
  this_scheduler = NULL;

  if(!ponyint_asio_start())
    return false;

  detect_quiescence = !library;

  uint32_t start;

  if(library)
  {
    start = 0;
    pony_register_thread();
  } else {
    start = 1;
    scheduler[0].tid = pony_thread_self();
  }

  for(uint32_t i = start; i < scheduler_count; i++)
  {
    if(!pony_thread_create(&scheduler[i].tid, run_thread, scheduler[i].cpu,
      &scheduler[i]))
      return false;
  }

  if(!library)
  {
    run_thread(&scheduler[0]);
    ponyint_sched_shutdown();
  }

  return true;
}

void ponyint_sched_stop()
{
  _atomic_store(&detect_quiescence, true);
  ponyint_sched_shutdown();
}

void ponyint_sched_add(pony_ctx_t* ctx, pony_actor_t* actor)
{
  if(ctx->scheduler != NULL)
  {
    // Add to the current scheduler thread.
    ponyint_mpmcq_push_single(ctx->scheduler->active_q, actor);
  } else {
    // Put on the shared mpmcq.
    ponyint_mpmcq_push(&inject, actor);
  }
}

uint32_t ponyint_sched_cores()
{
  return scheduler_count;
}

void pony_register_thread()
{
  if(this_scheduler != NULL)
    return;

  // Create a scheduler_t, even though we will only use the pony_ctx_t.
  this_scheduler = POOL_ALLOC(scheduler_t);
  memset(this_scheduler, 0, sizeof(scheduler_t));
  this_scheduler->tid = pony_thread_self();
}

pony_ctx_t* pony_ctx()
{
  return &this_scheduler->ctx;
}
