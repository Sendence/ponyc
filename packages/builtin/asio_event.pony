use @pony_asio_event_get_writeable[Bool](event: AsioEventID)
use @pony_asio_event_set_writeable[None](event: AsioEventID, writeable: Bool)
use @pony_asio_event_get_readable[Bool](event: AsioEventID)
use @pony_asio_event_set_readable[None](event: AsioEventID, readable: Bool)

type AsioEventID is Pointer[AsioEvent] tag

interface tag AsioEventNotify
  be _event_notify(event: AsioEventID, flags: U32, arg: U32)

primitive AsioEvent
  """
  Functions for asynchronous event notification.
  """
  fun none(): AsioEventID =>
    """
    An empty event.
    """
    AsioEventID

  fun get_readable(ev: AsioEventID): Bool =>
    @pony_asio_event_get_readable(ev)

  fun set_readable(ev: AsioEventID, readable': Bool) =>
    @pony_asio_event_set_readable(ev, readable')

  fun get_writeable(ev: AsioEventID): Bool =>
    @pony_asio_event_get_writeable(ev)

  fun set_writeable(ev: AsioEventID, writeable': Bool) =>
    @pony_asio_event_set_writeable(ev, writeable')

  fun readable(flags: U32): Bool =>
    """
    Returns true if the flags contain the readable flag.
    """
    (flags and (1 << 0)) != 0

  fun writeable(flags: U32): Bool =>
    """
    Returns true if the flags contain the writeable flag.
    """
    (flags and (1 << 1)) != 0

  fun disposable(flags: U32): Bool =>
    """
    Returns true if the event should be disposed of.
    """
    flags == 0

  fun dispose(): U32 => 0
  fun read(): U32 => 1 << 0
  fun write(): U32 => 1 << 1
  fun timer(): U32 => 1 << 2
  fun signal(): U32 => 1 << 3
  fun oneshot(): U32 => 1 << 8
  fun read_write(): U32 => read() or write()
  fun read_write_oneshot(): U32 => read() or write() or oneshot()
