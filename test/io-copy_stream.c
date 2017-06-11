#include "mruby.h"
#include "mruby/string.h"
#include "mruby/error.h"
#include "mruby/data.h"
#include "mruby/class.h"
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

struct test_io {
  int fd;
  mrb_bool closed : 1;
};

static void test_io_free(mrb_state *mrb, void *ptr);
struct mrb_data_type test_io_type = { "IO", test_io_free };
static void
test_io_free(mrb_state *mrb, void *ptr)
{
  struct test_io *io = (struct test_io *)ptr;
  if (io != NULL) {
    if (2 < io->fd && !io->closed) {
      if (close(io->fd) == -1) {
        mrb_sys_fail(mrb, 0);
      }
    }
    mrb_free(mrb, io);
  }
}

/* Dependent on mrb_get_args '&' section */
static mrb_value
test_io_get_block(mrb_state *mrb)
{
  if (mrb->c->ci->argc < 0)
    return *(mrb->c->stack + 2);
  else
    return *(mrb->c->stack + mrb->c->ci->argc + 1);
}

static mrb_value
test_io_open_body(mrb_state *mrb, mrb_value self)
{
  mrb_value block = test_io_get_block(mrb);
  if (mrb_type(block) != MRB_TT_PROC) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "io-copy_stream test io bug!");
  }
  return mrb_yield(mrb, block, self);
}

static int
test_io_close_internal(struct test_io *io)
{
  if (!io->closed) {
    if (close(io->fd) == -1) {
      return -1;
    }
    io->closed = TRUE;
  }
  return 0;
}

static mrb_value
test_io_open_ensure(mrb_state *mrb, mrb_value self)
{
  struct test_io *io = (struct test_io *)mrb_get_datatype(mrb, self, &test_io_type);
  if (test_io_close_internal(io) == -1)
    mrb_sys_fail(mrb, 0);
  return mrb_nil_value();
}

static mrb_value
test_io_s_open(mrb_state *mrb, mrb_value mod)
{
  mrb_value path;
  mrb_value mode = mrb_nil_value();
  mrb_value opt = mrb_nil_value();
  mrb_value block = mrb_nil_value();
  char *p;
  int fd;
  mrb_value self;
  struct test_io *io;
  int o_mode = 0
#ifdef O_CLOEXEC
  | O_CLOEXEC
#endif
  ;

  mrb_get_args(mrb, "o|oo&", &path, &mode, &opt, &block);

  if (mrb_nil_p(mode))
    mode = mrb_str_new_lit(mrb, "r");
  else if (!mrb_string_p(mode))
    mode = mrb_convert_type(mrb, mode, MRB_TT_STRING, "String", "to_str");

  p = RSTRING_PTR(mode);
  switch (*p) {
    case 'r':
      o_mode |= O_RDONLY;
      break;
    case 'w':
      o_mode |= O_WRONLY|O_CREAT|O_TRUNC;
      break;
  }

  fd = open(RSTRING_PTR(path), o_mode);
  if (fd == -1)
    mrb_sys_fail(mrb, 0);

  self = mrb_instance_new(mrb, mrb_obj_value(mrb_class_get(mrb, "IO")));
  io = (struct test_io *)mrb_malloc(mrb, sizeof(struct test_io));
  io->fd = fd;
  io->closed = FALSE;
  DATA_TYPE(self) = &test_io_type;
  DATA_PTR(self) = io;

  if (mrb_nil_p(block)) {
    return self;
  }
  else {
    // return mrb_yield(mrb, block, self);
    return mrb_ensure(mrb, test_io_open_body, self, test_io_open_ensure, self);
  }
}

static mrb_value
test_io_fileno(mrb_state *mrb, mrb_value self)
{
  struct test_io *io = (struct test_io *)mrb_get_datatype(mrb, self, &test_io_type);
  return mrb_fixnum_value(io->fd);
}

static mrb_value
test_io_read(mrb_state *mrb, mrb_value self)
{
  struct test_io *io = (struct test_io *)mrb_get_datatype(mrb, self, &test_io_type);
  ssize_t ss;
  mrb_int len;
  mrb_value length = mrb_nil_value();
  mrb_value buffer = mrb_nil_value();
  char *buf;

  mrb_get_args(mrb, "|oo", &length, &buffer);

  if (mrb_nil_p(length)) {
    /* read all */
    char tmp[1024*16];

    if (mrb_nil_p(buffer)) {
      buffer = mrb_str_new_lit(mrb, "");
    }

    while (1) {
      ss = read(io->fd, tmp, sizeof(buf));
      if (ss == -1)
        mrb_sys_fail(mrb, 0);
      if (ss == 0)
        break;
      mrb_str_cat(mrb, buffer, tmp, ss);
    }
    return buffer;
  }

  len = mrb_int(mrb, length);
  if (mrb_nil_p(buffer)) {
    buffer = mrb_str_new(mrb, NULL, len);
  }

  buf = RSTRING_PTR(buffer);
  ss = read(io->fd, buf, len);
  if (ss == -1)
    mrb_sys_fail(mrb, 0);

  RSTR_SET_LEN(RSTRING(buffer), ss);

  if (ss == 0)
    return mrb_nil_value();

  return buffer;
}

static mrb_value
test_io_write(mrb_state *mrb, mrb_value self)
{
  mrb_value buf;
  struct test_io *io = (struct test_io *)mrb_get_datatype(mrb, self, &test_io_type);
  mrb_int bytes;
  mrb_get_args(mrb, "o", &buf);
  buf = mrb_str_to_str(mrb, buf);
  bytes = write(io->fd, RSTRING_PTR(buf), RSTRING_LEN(buf));
  if (bytes == -1)
    mrb_sys_fail(mrb, 0);

  return mrb_fixnum_value(bytes);
}

static mrb_value
test_io_seek(mrb_state *mrb, mrb_value self)
{
  struct test_io *io = (struct test_io *)mrb_get_datatype(mrb, self, &test_io_type);
  mrb_int offset;
  mrb_int opt = -1;
  mrb_int pos;

  mrb_get_args(mrb, "i|i", &offset, &opt);
  if (opt == -1) {
    opt = SEEK_SET;
  }
  pos = lseek(io->fd, offset, opt);
  if (pos == -1)
    mrb_sys_fail(mrb, 0);

  return mrb_fixnum_value(pos);
}

static mrb_value
test_io_pos_get(mrb_state *mrb, mrb_value self)
{
  struct test_io *io = (struct test_io *)mrb_get_datatype(mrb, self, &test_io_type);
  mrb_int pos;

  pos = lseek(io->fd, 0, 1);
  if (pos == -1)
    mrb_sys_fail(mrb, 0);

  return mrb_fixnum_value(pos);
}

static mrb_value
test_io_pos_set(mrb_state *mrb, mrb_value self)
{
  struct test_io *io = (struct test_io *)mrb_get_datatype(mrb, self, &test_io_type);
  mrb_int offset;
  mrb_int pos;

  mrb_get_args(mrb, "i", &offset);

  pos = lseek(io->fd, offset, 0);
  if (pos == -1)
    mrb_sys_fail(mrb, 0);

  return mrb_fixnum_value(pos);
}

void mrb_mruby_io_copy_stream_gem_test(mrb_state *mrb)
{
  struct RClass *io = mrb_define_class(mrb, "IO", mrb->object_class);
  MRB_SET_INSTANCE_TT(io, MRB_TT_DATA);
  mrb_define_class(mrb, "File", io);
  mrb_define_class_method(mrb, io, "open", test_io_s_open, MRB_ARGS_ANY());
  mrb_define_method(mrb, io, "fileno", test_io_fileno, MRB_ARGS_ANY());
  mrb_define_method(mrb, io, "read", test_io_read, MRB_ARGS_ANY());
  mrb_define_method(mrb, io, "write", test_io_write, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, io, "seek", test_io_seek, MRB_ARGS_ANY());
  mrb_define_method(mrb, io, "pos", test_io_pos_get, MRB_ARGS_NONE());
  mrb_define_method(mrb, io, "pos=", test_io_pos_set, MRB_ARGS_REQ(1));
}
