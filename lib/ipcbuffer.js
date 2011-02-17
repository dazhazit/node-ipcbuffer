
var _IPCbuffer = require(__dirname+"/_ipcbuffer")._IPCbuffer;

function toHex(n) {
  if (n < 16) return '0' + n.toString(16);
  return n.toString(16);
}


_IPCbuffer.prototype.inspect = function() {
  var out = [],
      len = this.length;
  for (var i = 0; i < len; i++) {
    out[i] = toHex(this[i]);
  }
  return '<_IPCbuffer ' + out.join(' ') + '>';
};


_IPCbuffer.prototype.toString = function(encoding, start, end) {
  encoding = String(encoding || 'utf8').toLowerCase();
  start = +start || 0;
  if (typeof end == 'undefined') end = this.length;

  // Fastpath empty strings
  if (+end == start) {
    return '';
  }

  switch (encoding) {
    case 'utf8':
    case 'utf-8':
      return this.utf8Slice(start, end);

    case 'ascii':
      return this.asciiSlice(start, end);

    case 'binary':
      return this.binarySlice(start, end);

    case 'base64':
      return this.base64Slice(start, end);

    default:
      throw new Error('Unknown encoding');
  }
};


_IPCbuffer.prototype.write = function(string, offset, encoding) {
  // Support both (string, offset, encoding)
  // and the legacy (string, encoding, offset)
  if (!isFinite(offset)) {
    var swap = encoding;
    encoding = offset;
    offset = swap;
  }

  offset = +offset || 0;
  encoding = String(encoding || 'utf8').toLowerCase();

  switch (encoding) {
    case 'utf8':
    case 'utf-8':
      return this.utf8Write(string, offset);

    case 'ascii':
      return this.asciiWrite(string, offset);

    case 'binary':
      return this.binaryWrite(string, offset);

    case 'base64':
      return this.base64Write(string, offset);

    default:
      throw new Error('Unknown encoding');
  }
};


// slice(start, end)
_IPCbuffer.prototype.slice = function(start, end) {
  if (end > this.length) {
    throw new Error('oob');
  }
  if (start > end) {
    throw new Error('oob');
  }

  return new Buffer(this, end - start, +start);
};

// Buffer

function Buffer(subject, encoding, offset, ipc) {
  if (!(this instanceof Buffer)) {
    return new Buffer(subject, encoding, offset, ipc);
  }

  var type,pages,free;
  if (subject instanceof Buffer) {
    this.parent = subject;
  } else if (subject instanceof Array) {
    this.length = subject.length;	// From another buffer or Octets
  } else if ((type = typeof(subject)) === "number") {
    this.length = subject;		// Blank new Buffer
  } else if (type !== "string") {
    throw new Error("First argument needs to be a Buffer(to clone), "
		    +"number(length), array(Octets) or string(to copy).");
  }
  this.encoding = "utf8";
  if ((type = typeof(encoding)) === "number") {	// offset or ipc key
    if (subject instanceof Buffer) {
      this.offset = encoding;
    } else if(encoding) {
      this.ipc = encoding;	// An offset without a parent is meaningless
    }
  } else if (type === "string") {
    if(encoding.match(/ascii|binary|base64|utf/i)){
      this.encoding = encoding;
    }else{
      this.ipc = encoding;	// This is an IPC filename
    }
  } else if (arguments.length > 1) {
    throw new Error("Argument 2 needs to be a number(Offset or ipc key) "
		    +"or string(Encoding or ipc filename).");
  }
  if (((type = typeof(offset)) === "number") && (subject instanceof Buffer)) {
      this.offset = offset;
  } else if (((type === "string") || (type === "number")) && arguments.length === 3) {
      this.ipc = offset;
  } else if (arguments.length > 2) {
    throw new Error("Argument 3 needs to be a number(Offset or ipc key) "
		    +"or string(ipc filename).");
  }
  if (((type = typeof(ipc)) === "number") || (type === "string")) {
    this.ipc = ipc;
  } else if(arguments.length > 3) {
    throw new Error("Argument 4 needs to be a number(ipc key) "
		    +"or string(ipc filename).");
  }
  if (!this.length && (typeof(subject) === "string")) {
    this.length = Buffer.byteLength(subject,encoding);
  }

  if (this.ipc) {
      this.parent = new _IPCbuffer(this.length,this.ipc);
      this.offset = 0;
  } else if (this.length > Buffer.poolSize) {
    // Big buffer, just alloc one.
    this.parent = new _IPCbuffer(this.length, this.ipc);
    this.offset = 0;
  } else {
    // Small buffer.
    if (!pool || pool.length - pool.used < this.length) allocPool();
    this.parent = pool;
    this.offset = pool.used;
    pool.used += this.length;
  }

  // Assume object is an array
  if (Array.isArray(subject)) {
    for (var i = 0; i < this.length; i++) {
      this.parent[i + this.offset] = subject[i];
    }
  } else if (type == 'string') {
    // We are a string
    this.length = this.write(subject, 0, encoding);
  }


  _IPCbuffer.makeFastBuffer(this.parent, this, this.offset, this.length);
}

Buffer.prototype.PageSize = 4096;	// OS/Hardware dependant

Buffer.poolSize = 8 * 1024;
var pool;

function allocPool() {
  pool = new _IPCbuffer(Buffer.poolSize,null);
  pool.used = 0;
}


// Static methods
Buffer.isBuffer = function isBuffer(b) {
  return b instanceof Buffer || b instanceof _IPCbuffer;
};


// Inspect
Buffer.prototype.inspect = function inspect() {
  var out = [],
      len = this.length;
  for (var i = 0; i < len; i++) {
    out[i] = toHex(this.parent[i + this.offset]);
  }
  return '<Buffer ' + out.join(' ') + '>';
};


Buffer.prototype.get = function get(i) {
  if (i < 0 || i >= this.length) throw new Error('oob');
  return this.parent[this.offset + i];
};


Buffer.prototype.set = function set(i, v) {
  if (i < 0 || i >= this.length) throw new Error('oob');
  return this.parent[this.offset + i] = v;
};


// write(string, offset = 0, encoding = 'utf8')
Buffer.prototype.write = function(string, offset, encoding) {
  if (!isFinite(offset)) {
    var swap = encoding;
    encoding = offset;
    offset = swap;
  }

  offset = +offset || 0;
  encoding = String(encoding || 'utf8').toLowerCase();

  // Make sure we are not going to overflow
  var maxLength = this.length - offset;

  var ret;
  switch (encoding) {
    case 'utf8':
    case 'utf-8':
      ret = this.parent.utf8Write(string, this.offset + offset, maxLength);
      break;

    case 'ascii':
      ret = this.parent.asciiWrite(string, this.offset + offset, maxLength);
      break;

    case 'binary':
      ret = this.parent.binaryWrite(string, this.offset + offset, maxLength);
      break;

    case 'base64':
      // Warning: maxLength not taken into account in base64Write
      ret = this.parent.base64Write(string, this.offset + offset, maxLength);
      break;

    default:
      throw new Error('Unknown encoding');
  }

  Buffer._charsWritten = _IPCbuffer._charsWritten;

  return ret;
};


// toString(encoding, start=0, end=buffer.length)
Buffer.prototype.toString = function(encoding, start, end) {
  encoding = String(encoding || 'utf8').toLowerCase();

  if (start === undefined || !start || start < 0) {
    start = 0;
  } else if (start > this.length) {
    start = this.length;
  }

  if (end === undefined || !end || end > this.length) {
    end = this.length;
  } else if (end < 0) {
    end = 0;
  }

  start = start + this.offset;
  end = end + this.offset;

  switch (encoding) {
    case 'utf8':
    case 'utf-8':
      return this.parent.utf8Slice(start, end);

    case 'ascii':
      return this.parent.asciiSlice(start, end);

    case 'binary':
      return this.parent.binarySlice(start, end);

    case 'base64':
      return this.parent.base64Slice(start, end);

    default:
      throw new Error('Unknown encoding');
  }
};


// byteLength
Buffer.byteLength = _IPCbuffer.byteLength;


// copy(targetBuffer, targetStart=0, sourceStart=0, sourceEnd=buffer.length)
Buffer.prototype.copy = function(target, target_start, start, end) {
  var source = this;
  start || (start = 0);
  end || (end = this.length);
  target_start || (target_start = 0);

  if (end < start) throw new Error('sourceEnd < sourceStart');

  // Copy 0 bytes; we're done
  if (end === start) return 0;
  if (target.length == 0 || source.length == 0) return 0;

  if (target_start < 0 || target_start >= target.length) {
    throw new Error('targetStart out of bounds');
  }

  if (start < 0 || start >= source.length) {
    throw new Error('sourceStart out of bounds');
  }

  if (end < 0 || end > source.length) {
    throw new Error('sourceEnd out of bounds');
  }

  // Are we oob?
  if (end > this.length) {
    end = this.length;
  }

  if (target.length - target_start < end - start) {
    end = target.length - target_start + start;
  }

  return this.parent.copy(target.parent,
                          target_start + target.offset,
                          start + this.offset,
                          end + this.offset);
};


// slice(start, end)
Buffer.prototype.slice = function(start, end) {
  if (end === undefined) end = this.length;
  if (end > this.length) throw new Error('oob');
  if (start > end) throw new Error('oob');

  return new Buffer(this.parent, end - start, +start + this.offset);
};


// Legacy methods for backwards compatibility.

Buffer.prototype.utf8Slice = function(start, end) {
  return this.toString('utf8', start, end);
};

Buffer.prototype.binarySlice = function(start, end) {
  return this.toString('binary', start, end);
};

Buffer.prototype.asciiSlice = function(start, end) {
  return this.toString('ascii', start, end);
};

Buffer.prototype.utf8Write = function(string, offset) {
  return this.write(string, offset, 'utf8');
};

Buffer.prototype.binaryWrite = function(string, offset) {
  return this.write(string, offset, 'binary');
};

Buffer.prototype.asciiWrite = function(string, offset) {
  return this.write(string, offset, 'ascii');
};

exports._IPCbuffer = _IPCbuffer;
exports.Buffer = Buffer;
