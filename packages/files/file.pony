use "collections"

primitive _FileHandle

primitive FileOK
primitive FileError
primitive FileEOF
primitive FileBadFileNumber
primitive FileExists
primitive FilePermissionDenied

primitive _EBADF
  fun apply(): I32 => 9

primitive _EEXIST
  fun apply(): I32 => 17

primitive _EACCES
  fun apply(): I32 => 13

type FileErrNo is
  ( FileOK
  | FileError
  | FileEOF
  | FileBadFileNumber
  | FileExists
  | FilePermissionDenied
  )

primitive CreateFile
  """
  Open a File for read/write, creating if it doesn't exist, preserving the
  contents if it does exist.
  """
  fun apply(from: FilePath): (File | FileErrNo) =>
    let file = File(from)
    let err = file.errno()

    match err
    | FileOK => file
    else
      err
    end

primitive OpenFile
  """
  Open a File for read only.
  """
  fun apply(from: FilePath): (File | FileErrNo) =>
    let file = File.open(from)
    let err = file.errno()

    match err
    | FileOK => file
    else
      err
    end

class File
  """
  Operations on a file.
  """
  let path: FilePath
  let writeable: Bool
  let _newline: String = "\n"
  var _unsynced_data: Bool = false
  var _unsynced_metadata: Bool = false
  var _fd: I32
  var _last_line_length: USize = 256
  var _errno: FileErrNo = FileOK
  embed _pending_writev: Array[USize] = _pending_writev.create()
  var _pending_writev_total: USize = 0

  new create(from: FilePath) =>
    """
    Attempt to open for read/write, creating if it doesn't exist, preserving
    the contents if it does exist.
    Set errno according to result.
    """
    path = from
    writeable = true
    _fd = -1

    if not from.caps(FileRead) or not from.caps(FileWrite) then
      _errno = FileError
    else
      var flags: I32 = @ponyint_o_rdwr()
      let mode = FileMode._os() // default file permissions
      if not path.exists() then
        if not path.caps(FileCreate) then
          _errno = FileError
        else
          flags = flags or @ponyint_o_creat() or @ponyint_o_trunc()
        end
      end

      _fd = @open[I32](path.path.cstring(), flags, mode)

      if _fd == -1 then
        _errno = _get_error()
      else
        try
          _FileDes.set_rights(_fd, path, writeable)
        else
          _errno = FileError
        end
      end
    end

  new open(from: FilePath) =>
    """
    Open for read only.
    Set _errno according to result.
    """
    path = from
    writeable = false
    _fd = -1

    if
      not path.caps(FileRead) or
      try
        let info' = FileInfo(path)
        info'.directory or info'.pipe
      else
        true
      end
    then
      _errno = FileError
    else
      _fd = @open[I32](
        path.path.cstring(), @ponyint_o_rdwr())

      if _fd == -1 then
        _errno = FileError
      else
        try
          _FileDes.set_rights(_fd, path, writeable)
        else
          _errno = FileError
        end
      end
    end

  new _descriptor(fd: I32, from: FilePath) ? =>
    """
    Internal constructor from a file descriptor and a path.
    """
    if not from.caps(FileRead) or (fd == -1) then
      error
    end

    path = from
    writeable = from.caps(FileRead)
    _fd = fd

    _FileDes.set_rights(_fd, path, writeable)

  fun errno(): FileErrNo =>
    """
    Returns the last error code set for this File
    """
    _errno

  fun ref clear_errno() =>
    """
    Clears the last error code set for this File.
    Clears the error indicator for the stream.
    """
    _errno = FileOK

  fun _get_error(): FileErrNo =>
    """
    Fetch errno from the OS.
    """
    let os_errno = @pony_os_errno[I32]()
    match os_errno
    | _EBADF() => return FileBadFileNumber
    | _EEXIST() => return FileExists
    | _EACCES() => return FilePermissionDenied
    else
      return FileError
    end


  fun valid(): Bool =>
    """
    Returns true if the file is currently open.
    """
    not _fd == -1

  fun get_fd(): I32 ? =>
    """
    Returns the underlying file descriptor.
    Raises an error if the file is not currently open.
    """
    if _fd == -1 then
      error
    end

    _fd

  fun ref line(): String iso^ ? =>
    """
    Returns a line as a String. The newline is not included in the string. If
    there is no more data, this raises an error. If there is a file error,
    this raises an error.
    """
    if _fd == -1 then
      error
    end

    let bytes_to_read: USize = 1
    var offset: USize = 0
    var len = _last_line_length
    var result = recover String end
    var done = false

    while not done do
      result.reserve(len)

      let r = @read[ISize](_fd, result.cstring().usize() + offset, bytes_to_read)

      if r < bytes_to_read.isize() then
        _errno = if r == 0 then
                   FileEOF
                 else
                   _get_error() // error
                   error
                 end
      end

      result.recalc()

      done = try
        (_errno is FileEOF) or (result.at_offset(offset.isize()) == '\n')
      else
        true
      end

      if (not done) then
        offset = offset + 1
        if offset == len then
          len = len * 2
        end
      end
    end

    if result.size() == 0 then
      error
    end

    try
      if result.at_offset(offset.isize()) == '\n' then
        result.truncate(result.size() - 1)

        if result.at_offset(-1) == '\r' then
          result.truncate(result.size() - 1)
        end
      end
    end

    _last_line_length = len
    result

  fun ref read(len: USize): Array[U8] iso^ =>
    """
    Returns up to len bytes.
    """
    if _fd != -1 then
      let result = recover Array[U8].undefined(len) end

      let r = @read[ISize](_fd, result.cpointer(), len)

      if r < len.isize() then
        _errno = if r == 0 then
                   FileEOF
                 else
                   _get_error() // error
                 end
      end

      result.truncate(r.usize())
      result
    else
      recover Array[U8] end
    end

  fun ref read_string(len: USize): String iso^ =>
    """
    Returns up to len bytes. The resulting string may have internal null
    characters.
    """
    if _fd != -1 then
      let result = recover String(len) end

      let r = @read[ISize](_fd, result.cstring(), result.space())

      if r < len.isize() then
        _errno = if r == 0 then
                   FileEOF
                 else
                   _get_error() // error
                 end
      end

      result.truncate(r.usize())
      result
    else
      recover String end
    end

  fun ref print(data: ByteSeq box): Bool =>
    """
    Same as write, buts adds a newline.
    """
    _pending_writev.push(data.cpointer().usize()).push(data.size())
    _pending_writev_total = _pending_writev_total + data.size()

    _pending_writev.push(_newline.cpointer().usize()).push(_newline.size())
    _pending_writev_total = _pending_writev_total + _newline.size()

    _pending_writes()

  fun ref printv(data: ByteSeqIter box): Bool =>
    """
    Print an iterable collection of ByteSeqs.
    """
    for bytes in data.values() do
      _pending_writev.push(bytes.cpointer().usize()).push(bytes.size())
      _pending_writev_total = _pending_writev_total + bytes.size()

      _pending_writev.push(_newline.cpointer().usize()).push(_newline.size())
      _pending_writev_total = _pending_writev_total + _newline.size()
    end

    _pending_writes()

  fun ref write(data: ByteSeq box): Bool =>
    """
    Returns false if the file wasn't opened with write permission.
    Returns false and closes the file if not all the bytes were written.
    """
    _pending_writev.push(data.cpointer().usize()).push(data.size())
    _pending_writev_total = _pending_writev_total + data.size()

    _pending_writes()

  fun ref writev(data: ByteSeqIter box): Bool =>
    """
    Write an iterable collection of ByteSeqs.
    """
    for bytes in data.values() do
      _pending_writev.push(bytes.cpointer().usize()).push(bytes.size())
      _pending_writev_total = _pending_writev_total + bytes.size()
    end

    _pending_writes()

  fun ref _pending_writes(): Bool =>
    """
    Write pending data.
    Returns false if the file wasn't opened with write permission.
    Returns false and closes the file and discards all pending data
    if not all the bytes were written.
    Returns true if it sent all pending data.
    """
    // TODO: Make writev_batch_size user configurable
    let writev_batch_size: USize = @pony_os_writev_max[I32]().usize()
    var num_to_send: USize = 0
    var bytes_to_send: USize = 0
    while writeable and (_pending_writev_total > 0)
      and (_fd != -1) do
      try
        //determine number of bytes and buffers to send
        if (_pending_writev.size()/2) < writev_batch_size then
          num_to_send = _pending_writev.size()/2
          bytes_to_send = _pending_writev_total
        else
          //have more buffers than a single writev can handle
          //iterate over buffers being sent to add up total
          num_to_send = writev_batch_size
          bytes_to_send = 0
          for d in Range[USize](1, num_to_send*2, 2) do
            bytes_to_send = bytes_to_send + _pending_writev(d)
          end
        end

        // Write as much data as possible.
        var len = @writev[ISize](_fd,
          _pending_writev.cpointer(), num_to_send)

        if len < bytes_to_send.isize() then
          // sent less than requested
          // TODO: error recovery? EINTR?

          // check error
          _errno = _get_error()

          dispose()
          return false
        else
          // sent all data we requested in this batch
          _pending_writev_total = _pending_writev_total - bytes_to_send
          if _pending_writev_total == 0 then
            _pending_writev.clear()
            _unsynced_data = true
            _unsynced_metadata = true
            return true
          else
            for d in Range[USize](0, num_to_send, 1) do
              _pending_writev.shift()
              _pending_writev.shift()
            end
          end
        end
      else
        // TODO: error recovery?

        // check error
        _errno = _get_error()

        dispose()
        return false
      end
    end

    false

  fun ref position(): USize =>
    """
    Return the current cursor position in the file.
    """
    if _fd != -1 then
      let o: ISize = 0
      let b: I32 = 1
      let r = @lseek[ISize](_fd, o, b)

      if r < 0 then
        _errno = _get_error()
      end
      r.usize()
    else
      0
    end

  fun ref size(): USize =>
    """
    Return the total length of the file.
    """
    let pos = position()
    _seek(0, 2)
    let len = position()
    _seek(pos.isize(), 0)
    len

  fun ref seek_start(offset: USize): File =>
    """
    Set the cursor position relative to the start of the file.
    """
    if path.caps(FileSeek) then
      _seek(offset.isize(), 0)
    end
    this

  fun ref seek_end(offset: USize): File =>
    """
    Set the cursor position relative to the end of the file.
    """
    if path.caps(FileSeek) then
      _seek(-offset.isize(), 2)
    end
    this

  fun ref seek(offset: ISize): File =>
    """
    Move the cursor position.
    """
    if path.caps(FileSeek) then
      _seek(offset, 1)
    end
    this

  fun ref sync(): File =>
    """
    Sync the file contents to physical storage.
    """
    if path.caps(FileSync) and (_fd != -1) then
      let r = @fsync[I32](_fd)
      if r < 0 then
        _errno = _get_error()
      end
    end
    _unsynced_data = false
    _unsynced_metadata = false
    this

  fun ref datasync(): File =>
    """
    Sync the file contents to physical storage.
    """
    if path.caps(FileSync) and (_fd != -1) then
      let r = @fdatasync[I32](_fd)
      if r < 0 then
        _errno = _get_error()
      end
    end
    _unsynced_data = false
    this

  fun ref set_length(len: USize): Bool =>
    """
    Change the file size. If it is made larger, the new contents are undefined.
    """
    if path.caps(FileTruncate) and writeable and (_fd != -1) then
      let pos = position()
      let result = ifdef windows then
        @_chsize_s[I32](_fd, len)
      else
        @ftruncate[I32](_fd, len)
      end

      if pos >= len then
        _seek(0, 2)
      end

      if result == 0 then
        true
      else
        _errno = _get_error()
        false
      end
    else
      false
    end

  fun info(): FileInfo ? =>
    """
    Return a FileInfo for this directory. Raise an error if the fd is invalid
    or if we don't have FileStat permission.
    """
    FileInfo._descriptor(_fd, path)

  fun chmod(mode: FileMode box): Bool =>
    """
    Set the FileMode for this directory.
    """
    _FileDes.chmod(_fd, path, mode)

  fun chown(uid: U32, gid: U32): Bool =>
    """
    Set the owner and group for this directory. Does nothing on Windows.
    """
    _FileDes.chown(_fd, path, uid, gid)

  fun touch(): Bool =>
    """
    Set the last access and modification times of the directory to now.
    """
    _FileDes.touch(_fd, path)

  fun set_time(atime: (I64, I64), mtime: (I64, I64)): Bool =>
    """
    Set the last access and modification times of the directory to the given
    values.
    """
    _FileDes.set_time(_fd, path, atime, mtime)

  fun ref lines(): FileLines =>
    """
    Returns an iterator for reading lines from the file.
    """
    FileLines(this)

  fun ref dispose() =>
    """
    Close the file. Future operations will do nothing.
    """
    if _fd != -1 then
      if _unsynced_data or _unsynced_metadata then
        sync()
      end
      let r = @close[I32](_fd)
      if r < 0 then
        _errno = _get_error()
      end
      _fd = -1

      _pending_writev_total = 0
      _pending_writev.clear()
    end

  fun ref _seek(offset: ISize, base: I32) =>
    """
    Move the cursor position.
    """
    if _fd != -1 then
      let r = @lseek[I32](_fd, offset, base)
      if r < 0 then
        _errno = _get_error()
      end
    end

  fun _final() =>
    """
    Close the file.
    """
    // TODO: How to handle errors on sync/close?
    if _fd != -1 then
      if _unsynced_data or _unsynced_metadata then
        // TODO: Can't check for FileSync in `_final`
        //       what to do instead for unsync'd data?
//        if path.caps(FileSync) then
//          @fsync[I32](_fd)
          None
//        end
      end
      @close[I32](_fd)
    end

class FileLines is Iterator[String]
  """
  Iterate over the lines in a file.
  """
  var _file: File
  var _line: String = ""
  var _next: Bool = false

  new create(file: File) =>
    _file = file

    try
      _line = file.line()
      _next = true
    end

  fun ref has_next(): Bool =>
    _next

  fun ref next(): String =>
    let r = _line

    try
      _line = _file.line()
    else
      _next = false
    end

    r
