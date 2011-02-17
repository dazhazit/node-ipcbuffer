
#include <node.h>
#include "ipcbuffer.h"

#include <v8.h>

#include <assert.h>
#include <stdlib.h> // malloc, free
#include <string.h> // memcpy

#ifdef __MINGW32__
# include <platform.h>
# include <platform_win32_winsock.h> // htons, htonl
#endif

#if __POSIX__ || __SYSV__
# include <arpa/inet.h> // htons, htonl
// For shared memory
# include <unistd.h>
# include <fcntl.h>
#ifdef __SYSV__
# include <sys/ipc.h>
# include <sys/shm.h>	// shmat, shmdt etc...
#endif
#ifdef __POSIX__
# include <sys/mman.h>	// mmap, munmap...
#endif
#endif



#define MIN(a,b) ((a) < (b) ? (a) : (b))

namespace node {

using namespace v8;

#define SLICE_ARGS(start_arg, end_arg)                               \
  if (!start_arg->IsInt32() || !end_arg->IsInt32()) {                \
    return ThrowException(Exception::TypeError(                      \
          String::New("Bad argument.")));                            \
  }                                                                  \
  int32_t start = start_arg->Int32Value();                           \
  int32_t end = end_arg->Int32Value();                               \
  if (start < 0 || end < 0) {                                        \
    return ThrowException(Exception::TypeError(                      \
          String::New("Bad argument.")));                            \
  }                                                                  \
  if (!(start <= end)) {                                             \
    return ThrowException(Exception::Error(                          \
          String::New("Must have start <= end")));                   \
  }                                                                  \
  if ((size_t)end > parent->length_) {                               \
    return ThrowException(Exception::Error(                          \
          String::New("end cannot be longer than parent.length")));  \
  }


static Persistent<String> length_symbol;
static Persistent<String> chars_written_sym;
static Persistent<String> write_sym;
Persistent<FunctionTemplate> IPCbuffer::constructor_template;


static inline size_t base64_decoded_size(const char *src, size_t size) {
  const char *const end = src + size;
  const int remainder = size % 4;

  size = (size / 4) * 3;
  if (remainder) {
    if (size == 0 && remainder == 1) {
      // special case: 1-byte input cannot be decoded
      size = 0;
    } else {
      // non-padded input, add 1 or 2 extra bytes
      size += 1 + (remainder == 3);
    }
  }

  // check for trailing padding (1 or 2 bytes)
  if (size > 0) {
    if (end[-1] == '=') size--;
    if (end[-2] == '=') size--;
  }

  return size;
}


static size_t ByteLength (Handle<String> string, enum encoding enc) {
  HandleScope scope;

  if (enc == UTF8) {
    return string->Utf8Length();
  } else if (enc == BASE64) {
    String::Utf8Value v(string);
    return base64_decoded_size(*v, v.length());
  } else {
    return string->Length();
  }
}


Handle<Object> IPCbuffer::New(Handle<String> string) {
  HandleScope scope;

  // get Buffer from global scope.
  Local<Object> global = v8::Context::GetCurrent()->Global();
  Local<Value> bv = global->Get(String::NewSymbol("IPCbuffer"));
  assert(bv->IsFunction());
  Local<Function> b = Local<Function>::Cast(bv);

  Local<Value> argv[1] = { Local<Value>::New(string) };
  Local<Object> instance = b->NewInstance(1, argv);

  return scope.Close(instance);
}


IPCbuffer* IPCbuffer::New(size_t length) {
  HandleScope scope;

  Local<Value> arg = Integer::NewFromUnsigned(length);
  Local<Object> b = constructor_template->GetFunction()->NewInstance(1, &arg);

  return ObjectWrap::Unwrap<IPCbuffer>(b);
}


IPCbuffer* IPCbuffer::New(char* data, size_t length) {
  HandleScope scope;

  Local<Value> arg = Integer::NewFromUnsigned(0);
  Local<Object> obj = constructor_template->GetFunction()->NewInstance(1, &arg);

  IPCbuffer *buffer = ObjectWrap::Unwrap<IPCbuffer>(obj);
  buffer->Replace(data, length, NULL, NULL);

  return buffer;
}


IPCbuffer* IPCbuffer::New(char *data, size_t length,
                    free_callback callback, void *hint) {
  HandleScope scope;

  Local<Value> arg = Integer::NewFromUnsigned(0);
  Local<Object> obj = constructor_template->GetFunction()->NewInstance(1, &arg);

  IPCbuffer *buffer = ObjectWrap::Unwrap<IPCbuffer>(obj);
  buffer->Replace(data, length, callback, hint);

  return buffer;
}

Handle<Value> IPCbuffer::New(const Arguments &args) {
  if (!args.IsConstructCall()) {
    return FromConstructorTemplate(constructor_template, args);
  }

  HandleScope scope;

  uint32_t key = 0;
  char *filename = NULL;
  IPCbuffer *buffer;

  if (args[0]->IsInt32() || args[0]->IsNumber()) {
    // var buffer = new IPCbuffer(1024);
    size_t length = (args[0]->Uint32Value());
    if (args.Length() > 1) {
#if __SYSV__ || __POSIX__
      if (args[1]->IsUint32()) { // This is SYS V style key value
#if __SYSV__
	key = (uint32_t) args[1]->Uint32Value();
#else
        return ThrowException(Exception::RangeError(String::New(
	    "This OS can't handle System V (shmat style) shared memory")));
#endif
      } else if (args[1]->IsString()) { // Posix filename
#if __POSIX__
	String::Utf8Value path(args[1]->ToString());
	// I do a local copy of the filename as it's likely to dissappear
        filename = new char[(path.length()+1)];
        memcpy(filename,*path,path.length()+1);
#else
        return ThrowException(Exception::RangeError(String::New(
	    "This OS can't handle Posix (mmap style) shared memory")));
#endif
      } else if (!args[1]->IsUndefined() && !args[1]->IsNull()) {
	return ThrowException(Exception::TypeError(String::New(
	    "Only an integer or string filename can be used for shared memory")));
      }
#else
      return ThrowException(Exception::RangeError(String::New(
	  "This OS can't handle shared memory")));
#endif
    }
    buffer = new IPCbuffer(args.This(), length, filename, key);
  } else {
    return ThrowException(Exception::TypeError(String::New(
	"Length needs to be an integer")));
  }
  return args.This();
}


IPCbuffer::IPCbuffer(Handle<Object> wrapper, size_t length, char* path, uint32_t id) : ObjectWrap() {
  Wrap(wrapper);

  fileName_ = path;
  id_ = id;
  length_ = 0;
  callback_ = NULL;

  Replace(NULL, length, NULL, NULL);
}


IPCbuffer::~IPCbuffer() {
  Replace(NULL, 0, NULL, NULL);
  delete [] fileName_;
}

/*
 * This is where most of the allocation action takes place
 */
void IPCbuffer::Replace(char *data, size_t length,
                     free_callback callback, void *hint) {
  HandleScope scope;

  int32_t fd_;
#ifdef __SYSV__
  uint32_t shmid_;
#endif

  if (callback_) {
    callback_(data_, callback_hint_);
  } else if (length_) {
#ifdef __POSIX__
    if (fileName_ && data_) {  //About to close an open memory block
      if (fileName_[0] != '*'){
	msync(data_, length_, MS_ASYNC);  // Make sure it syncs
      }
      munmap(data_, length_);  // Unmap it.
      if (fileName_[0] == '*') {
	// Remove the shared block. But only if not open elsewhere
        shm_unlink(&fileName_[1]);
      }
    } else
#endif
#ifdef __SYSV__
    if (id_) {
      shmdt(data_);		// Detach SYS V memory
    } else
#endif
    {
      delete [] data_;
      V8::AdjustAmountOfExternalAllocatedMemory(-(sizeof(IPCbuffer) + length_));
    }
  }

  length_ = length;
  callback_ = callback;
  callback_hint_ = hint;

  if (callback_) {
    data_ = data;
  } else if (length_) {

#ifdef __POSIX__
    if (fileName_) {
      data_ = NULL;
      if (fileName_[0] == '*') { 		// Not file backed
        fd_ = shm_open(&fileName_[1], O_RDWR|O_CREAT, S_IWUSR|S_IRUSR);
      } else {				// File Backed
        fd_ = open(fileName_, O_RDWR|O_CREAT, S_IWUSR|S_IRUSR);
      }
      if (fd_ != -1) {
	ftruncate(fd_,length_);		// Bus Error avoidance
        if ((data_ = (char*) mmap(NULL, length_, PROT_READ|PROT_WRITE,
	    MAP_SHARED, fd_, 0)) <= (char*) 0) {
          close(fd_);
          ThrowException(Exception::Error(String::New(
	      "Couldn't create shared memory")));
	}
	close(fd_);	// We don't need fd anymore.
      } else {
        ThrowException(Exception::ReferenceError(String::New(
	    "Couldn't open share file")));
      }
    } else
#endif
#ifdef __SYSV__
    if (id_)  {
      if (((shmid_ = shmget((key_t)id_, length_, IPC_CREAT | 0666)) < 0) ||
          ((data_ = (char*) shmat(shmid_, NULL, 0)) == (char*) -1)) {
	data_ = NULL;
	perror("Shared ");
        ThrowException(Exception::ReferenceError(String::New(
	    "Couldn't create shared memory")));
      }
    } else
#endif
    {
      data_ = new char[length_];
      V8::AdjustAmountOfExternalAllocatedMemory(sizeof(IPCbuffer) + length_);
    }
    if (data && data_){
      memcpy(data_, data, length_);
    }
  } else {
    data_ = NULL;
  }

  handle_->SetIndexedPropertiesToExternalArrayData(data_,
                                                   kExternalUnsignedByteArray,
                                                   length_);
  handle_->Set(length_symbol, Integer::NewFromUnsigned(length_));
}


Handle<Value> IPCbuffer::BinarySlice(const Arguments &args) {
  HandleScope scope;
  IPCbuffer *parent = ObjectWrap::Unwrap<IPCbuffer>(args.This());
  SLICE_ARGS(args[0], args[1])

  char *data = parent->data_ + start;
  //Local<String> string = String::New(data, end - start);

  Local<Value> b =  Encode(data, end - start, BINARY);

  return scope.Close(b);
}


Handle<Value> IPCbuffer::AsciiSlice(const Arguments &args) {
  HandleScope scope;
  IPCbuffer *parent = ObjectWrap::Unwrap<IPCbuffer>(args.This());
  SLICE_ARGS(args[0], args[1])

  char* data = parent->data_ + start;
  Local<String> string = String::New(data, end - start);

  return scope.Close(string);
}


Handle<Value> IPCbuffer::Utf8Slice(const Arguments &args) {
  HandleScope scope;
  IPCbuffer *parent = ObjectWrap::Unwrap<IPCbuffer>(args.This());
  SLICE_ARGS(args[0], args[1])
  char *data = parent->data_ + start;
  Local<String> string = String::New(data, end - start);
  return scope.Close(string);
}

static const char *base64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz"
                                  "0123456789+/";
static const int unbase64_table[] =
  {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-2,-1,-1,-2,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63
  ,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1
  ,-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14
  ,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1
  ,-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40
  ,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  };
#define unbase64(x) unbase64_table[(uint8_t)(x)]


Handle<Value> IPCbuffer::Base64Slice(const Arguments &args) {
  HandleScope scope;
  IPCbuffer *parent = ObjectWrap::Unwrap<IPCbuffer>(args.This());
  SLICE_ARGS(args[0], args[1])

  int n = end - start;
  int out_len = (n + 2 - ((n + 2) % 3)) / 3 * 4;
  char *out = new char[out_len];

  uint8_t bitbuf[3];
  int i = start; // data() index
  int j = 0; // out index
  char c;
  bool b1_oob, b2_oob;

  while (i < end) {
    bitbuf[0] = parent->data_[i++];

    if (i < end) {
      bitbuf[1] = parent->data_[i];
      b1_oob = false;
    }  else {
      bitbuf[1] = 0;
      b1_oob = true;
    }
    i++;

    if (i < end) {
      bitbuf[2] = parent->data_[i];
      b2_oob = false;
    }  else {
      bitbuf[2] = 0;
      b2_oob = true;
    }
    i++;


    c = bitbuf[0] >> 2;
    assert(c < 64);
    out[j++] = base64_table[(int)c];
    assert(j < out_len);

    c = ((bitbuf[0] & 0x03) << 4) | (bitbuf[1] >> 4);
    assert(c < 64);
    out[j++] = base64_table[(int)c];
    assert(j < out_len);

    if (b1_oob) {
      out[j++] = '=';
    } else {
      c = ((bitbuf[1] & 0x0F) << 2) | (bitbuf[2] >> 6);
      assert(c < 64);
      out[j++] = base64_table[(int)c];
    }
    assert(j < out_len);

    if (b2_oob) {
      out[j++] = '=';
    } else {
      c = bitbuf[2] & 0x3F;
      assert(c < 64);
      out[j++]  = base64_table[(int)c];
    }
    assert(j <= out_len);
  }

  Local<String> string = String::New(out, out_len);
  delete [] out;
  return scope.Close(string);
}


// var bytesCopied = buffer.copy(target, targetStart, sourceStart, sourceEnd);
Handle<Value> IPCbuffer::Copy(const Arguments &args) {
  HandleScope scope;

  IPCbuffer *source = ObjectWrap::Unwrap<IPCbuffer>(args.This());

  if (!IPCbuffer::HasInstance(args[0])) {
    return ThrowException(Exception::TypeError(String::New(
            "First arg should be a Buffer")));
  }

  Local<Object> target = args[0]->ToObject();
  char *target_data = IPCbuffer::Data(target);
  size_t target_length = IPCbuffer::Length(target);

  size_t target_start = args[1]->Uint32Value();
  size_t source_start = args[2]->Uint32Value();
  size_t source_end = args[3]->IsUint32() ? args[3]->Uint32Value()
                                          : source->length_;

  if (source_end < source_start) {
    return ThrowException(Exception::Error(String::New(
            "sourceEnd < sourceStart")));
  }

  // Copy 0 bytes; we're done
  if (source_end == source_start) {
    return scope.Close(Integer::New(0));
  }

  if (target_start < 0 || target_start >= target_length) {
    return ThrowException(Exception::Error(String::New(
            "targetStart out of bounds")));
  }

  if (source_start < 0 || source_start >= source->length_) {
    return ThrowException(Exception::Error(String::New(
            "sourceStart out of bounds")));
  }

  if (source_end < 0 || source_end > source->length_) {
    return ThrowException(Exception::Error(String::New(
            "sourceEnd out of bounds")));
  }

  ssize_t to_copy = MIN(MIN(source_end - source_start,
                            target_length - target_start),
                            source->length_ - source_start);


  // need to use slightly slower memmove is the ranges might overlap
  memmove((void *)(target_data + target_start),
          (const void*)(source->data_ + source_start),
          to_copy);

  return scope.Close(Integer::New(to_copy));
}


// var charsWritten = buffer.utf8Write(string, offset, [maxLength]);
Handle<Value> IPCbuffer::Utf8Write(const Arguments &args) {
  HandleScope scope;
  IPCbuffer *buffer = ObjectWrap::Unwrap<IPCbuffer>(args.This());

  if (!args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New(
            "Argument must be a string")));
  }

  Local<String> s = args[0]->ToString();

  size_t offset = args[1]->Uint32Value();

  if (s->Length() > 0 && offset >= buffer->length_) {
    return ThrowException(Exception::TypeError(String::New(
            "Offset is out of bounds")));
  }

  size_t max_length = args[2]->IsUndefined() ? buffer->length_ - offset
                                             : args[2]->Uint32Value();
  max_length = MIN(buffer->length_ - offset, max_length);

  char* p = buffer->data_ + offset;

  int char_written;

  int written = s->WriteUtf8(p,
                             max_length,
                             &char_written,
                             String::HINT_MANY_WRITES_EXPECTED);

  constructor_template->GetFunction()->Set(chars_written_sym,
                                           Integer::New(char_written));

  if (written > 0 && p[written-1] == '\0') written--;

  return scope.Close(Integer::New(written));
}


// var charsWritten = buffer.asciiWrite(string, offset);
Handle<Value> IPCbuffer::AsciiWrite(const Arguments &args) {
  HandleScope scope;

  IPCbuffer *buffer = ObjectWrap::Unwrap<IPCbuffer>(args.This());

  if (!args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New(
            "Argument must be a string")));
  }

  Local<String> s = args[0]->ToString();

  size_t offset = args[1]->Uint32Value();

  if (s->Length() > 0 && offset >= buffer->length_) {
    return ThrowException(Exception::TypeError(String::New(
            "Offset is out of bounds")));
  }

  size_t max_length = args[2]->IsUndefined() ? buffer->length_ - offset
                                             : args[2]->Uint32Value();
  max_length = MIN((size_t)s->Length(), MIN(buffer->length_ - offset, max_length));

  char *p = buffer->data_ + offset;

  int written = s->WriteAscii(p,
                              0,
                              max_length,
                              String::HINT_MANY_WRITES_EXPECTED);
  return scope.Close(Integer::New(written));
}


// var bytesWritten = buffer.base64Write(string, offset, [maxLength]);
Handle<Value> IPCbuffer::Base64Write(const Arguments &args) {
  HandleScope scope;

  assert(unbase64('/') == 63);
  assert(unbase64('+') == 62);
  assert(unbase64('T') == 19);
  assert(unbase64('Z') == 25);
  assert(unbase64('t') == 45);
  assert(unbase64('z') == 51);

  assert(unbase64(' ') == -2);
  assert(unbase64('\n') == -2);
  assert(unbase64('\r') == -2);

  IPCbuffer *buffer = ObjectWrap::Unwrap<IPCbuffer>(args.This());

  if (!args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New(
            "Argument must be a string")));
  }

  String::AsciiValue s(args[0]->ToString());
  size_t offset = args[1]->Int32Value();

  // handle zero-length buffers graciously
  if (offset == 0 && buffer->length_ == 0) {
    return scope.Close(Integer::New(0));
  }

  if (offset >= buffer->length_) {
    return ThrowException(Exception::TypeError(String::New(
            "Offset is out of bounds")));
  }

  const size_t size = base64_decoded_size(*s, s.length());
  if (size > buffer->length_ - offset) {
    // throw exception, don't silently truncate
    return ThrowException(Exception::TypeError(String::New(
            "Buffer too small")));
  }

  char a, b, c, d;
  char* start = buffer->data_ + offset;
  char* dst = start;
  const char *src = *s;
  const char *const srcEnd = src + s.length();

  while (src < srcEnd) {
    int remaining = srcEnd - src;

    while (unbase64(*src) < 0 && src < srcEnd) {
      src++;
      remaining--;
    }
    if (remaining == 0 || *src == '=') break;
    a = unbase64(*src++);

    while (unbase64(*src) < 0 && src < srcEnd) {
      src++;
      remaining--;
    }
    if (remaining <= 1 || *src == '=') break;
    b = unbase64(*src++);
    *dst++ = (a << 2) | ((b & 0x30) >> 4);

    while (unbase64(*src) < 0 && src < srcEnd) {
      src++;
      remaining--;
    }
    if (remaining <= 2 || *src == '=') break;
    c = unbase64(*src++);
    *dst++ = ((b & 0x0F) << 4) | ((c & 0x3C) >> 2);

    while (unbase64(*src) < 0 && src < srcEnd) {
      src++;
      remaining--;
    }
    if (remaining <= 3 || *src == '=') break;
    d = unbase64(*src++);
    *dst++ = ((c & 0x03) << 6) | (d & 0x3F);
  }

  return scope.Close(Integer::New(dst - start));
}


Handle<Value> IPCbuffer::BinaryWrite(const Arguments &args) {
  HandleScope scope;

  IPCbuffer *buffer = ObjectWrap::Unwrap<IPCbuffer>(args.This());

  if (!args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New(
            "Argument must be a string")));
  }

  Local<String> s = args[0]->ToString();

  size_t offset = args[1]->Int32Value();

  if (s->Length() > 0 && offset >= buffer->length_) {
    return ThrowException(Exception::TypeError(String::New(
            "Offset is out of bounds")));
  }

  char *p = (char*)buffer->data_ + offset;

  size_t towrite = MIN((unsigned long) s->Length(), buffer->length_ - offset);

  int written = DecodeWrite(p, towrite, s, BINARY);
  return scope.Close(Integer::New(written));
}


// var nbytes = Buffer.byteLength("string", "utf8")
Handle<Value> IPCbuffer::ByteLength(const Arguments &args) {
  HandleScope scope;

  if (!args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New(
            "Argument must be a string")));
  }

  Local<String> s = args[0]->ToString();
  enum encoding e = ParseEncoding(args[1], UTF8);

  return scope.Close(Integer::New(node::ByteLength(s, e)));
}


Handle<Value> IPCbuffer::MakeFastBuffer(const Arguments &args) {
  HandleScope scope;

  IPCbuffer *buffer = ObjectWrap::Unwrap<IPCbuffer>(args[0]->ToObject());
  Local<Object> fast_buffer = args[1]->ToObject();;
  uint32_t offset = args[2]->Uint32Value();
  uint32_t length = args[3]->Uint32Value();

  fast_buffer->SetIndexedPropertiesToExternalArrayData(buffer->data_ + offset,
                                                      kExternalUnsignedByteArray,
                                                      length);

  return Undefined();
}


bool IPCbuffer::HasInstance(v8::Handle<v8::Value> val) {
  if (!val->IsObject()) return false;
  v8::Local<v8::Object> obj = val->ToObject();

  if (obj->GetIndexedPropertiesExternalArrayDataType() == kExternalUnsignedByteArray)
    return true;

  // Also check for SlowBuffers that are empty.
  if (constructor_template->HasInstance(obj))
    return true;

  return false;
}


void IPCbuffer::Initialize(Handle<Object> target) {
  HandleScope scope;

  length_symbol = Persistent<String>::New(String::NewSymbol("length"));
  chars_written_sym = Persistent<String>::New(String::NewSymbol("_charsWritten"));

  Local<FunctionTemplate> t = FunctionTemplate::New(IPCbuffer::New);
  constructor_template = Persistent<FunctionTemplate>::New(t);
  constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
  constructor_template->SetClassName(String::NewSymbol("_IPCbuffer"));

  // copy free
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "binarySlice", IPCbuffer::BinarySlice);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "asciiSlice", IPCbuffer::AsciiSlice);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "base64Slice", IPCbuffer::Base64Slice);
  // TODO NODE_SET_PROTOTYPE_METHOD(t, "utf16Slice", Utf16Slice);
  // copy
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "utf8Slice", IPCbuffer::Utf8Slice);

  NODE_SET_PROTOTYPE_METHOD(constructor_template, "utf8Write", IPCbuffer::Utf8Write);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "asciiWrite", IPCbuffer::AsciiWrite);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "binaryWrite", IPCbuffer::BinaryWrite);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "base64Write", IPCbuffer::Base64Write);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "copy", IPCbuffer::Copy);

  NODE_SET_METHOD(constructor_template->GetFunction(),
                  "byteLength",
                  IPCbuffer::ByteLength);
  NODE_SET_METHOD(constructor_template->GetFunction(),
                  "makeFastBuffer",
                  IPCbuffer::MakeFastBuffer);

  target->Set(String::NewSymbol("_IPCbuffer"), constructor_template->GetFunction());
}


}  // namespace node

NODE_MODULE(_ipcbuffer, node::IPCbuffer::Initialize);
