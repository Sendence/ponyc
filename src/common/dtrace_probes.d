provider pony {
  /**
   * Fired when a hashmap has a collision
   */
  probe hash__collision(uintptr_t hashmap);

  /**
   * Fired when a hashmap finds an entry
   */
  probe hash__success(uintptr_t hashmap, size_t size, size_t num_probes);

  /**
   * Fired when a hashmap doesn't find an entry
   */
  probe hash__fail(uintptr_t hashmap, size_t size, size_t num_probes);

  /**
   * Fired when a hashmap doesn't find an entry
   */
  probe hash__resize(uintptr_t from, uintptr_t to);

  /**
   * Fired when a actor is being created
   */
  probe actor__alloc(uintptr_t scheduler, uintptr_t actor, unsigned int type_id);

  /**
   * Fired when a message is being send
   * @param id the message id
   */
  probe actor__msg__send(uintptr_t scheduler, uint32_t id, uintptr_t from_actor, uintptr_t to_actor);

  /**
   * Fired when a message is being run by an actor
   * @param actor the actor running the message
   * @param id the message id
   */
  probe actor__msg__run(uintptr_t scheduler, uintptr_t actor, uint32_t id);

  /**
   * Fired when actor is scheduled
   * @param scheduler is the scheduler that scheduled the actor
   * @param actor is the scheduled actor
   */
  probe actor__scheduled(uintptr_t scheduler, uintptr_t actor);

  /**
   * Fired when actor is descheduled
   * @param scheduler is the scheduler that descheduled the actor
   * @param actor is the descheduled actor
   */
  probe actor__descheduled(uintptr_t scheduler, uintptr_t actor);

  /**
   * Fired when cpu goes into nanosleep
   * @param ns is nano seconds spent in sleep
   */
  probe cpu__nanosleep(uint64_t ns);

  /**
   * Fired when the garbage collection function is ending
   */
  probe gc__end(uintptr_t scheduler, uintptr_t actor, unsigned long heap_size);

  /**
   * Fired when the garbage collector finishes sending an object
   */
  probe gc__send__end(uintptr_t scheduler);

  /**
   * Fired when the garbage collector stats sending an object
   */
  probe gc__send__start(uintptr_t scheduler);

  /**
   * Fired when the garbage collector finishes receiving an object
   */
  probe gc__recv__end(uintptr_t scheduler);

  /**
   * Fired when the garbage collector starts receiving an object
   */
  probe gc__recv__start(uintptr_t scheduler);

  /**
   * Fired when the garbage collection function has started
   */
  probe gc__start(uintptr_t scheduler, uintptr_t actor, unsigned long heap_size);

  /**
   * Fired when the garbage collection threshold is changed with a certain factor
   * @param factor the factor with which the GC threshold is changed
   */
  probe gc__threshold(double factor);

  /**
   * Fired when memory is allocated on the heap
   * @param size the size of the allocated memory
   */
  probe heap__alloc(uintptr_t scheduler, unsigned long size, uintptr_t actor, unsigned long heap_size);

  /**
   * Fired when runtime initiates
   */
  probe rt__init();

  /**
   * Fired when runtime is initiated and the program starts
   */
  probe rt__start();

  /**
   * Fired when runtime shutdown is started
   */
  probe rt__end();

  /**
   * Fired when a scheduler succesfully steals a job
   * @param scheduler is the scheduler that stole the job
   * @param victim is the victim that the scheduler stole from
   * @param actor is actor that was stolen from the victim
   */
  probe work__steal__successful(uintptr_t scheduler, uintptr_t victim, uintptr_t actor);

  /**
   * Fired when a scheduler fails to steal a job
   * @param scheduler is the scheduler that attempted theft
   * @param victim is the victim that the scheduler attempted to steal from
   */
  probe work__steal__failure(uintptr_t scheduler, uintptr_t victim);

};
