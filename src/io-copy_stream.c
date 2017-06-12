#include "config.h"

#include "mruby.h"
#include "mruby/data.h"
#include "mruby/string.h"
#include "mruby/error.h"
#include "mruby/throw.h"
#include <string.h>
#if defined(HAVE_FCNTL_H) || defined(_WIN32)
#include <fcntl.h>
#elif defined(HAVE_SYS_FCNTL_H)
#include <sys/fcntl.h>
#endif
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/stat.h>

/*
original is ruby/ruby/io.c
*/

#ifdef HAVE_SENDFILE
# ifdef __linux__
#  define USE_SENDFILE 1
#  ifdef HAVE_SYS_SENDFILE_H
#   include <sys/sendfile.h>
#  endif
# endif
#endif

#if !HAVE_OFF_T && !defined(off_t)
# define off_t long
#endif

struct copy_stream_struct {
  mrb_value src;
  mrb_value dst;
  off_t copy_length; /* (off_t)-1 if not specified */
  off_t src_offset; /* (off_t)-1 if not specified */
  int src_fd;
  int dst_fd;
  mrb_bool close_src : 1;
  mrb_bool close_dst : 1;
  mrb_int total;
  const char *syserr;
  int error_no;
  const char *notimp;
  fd_set *fds;
  // mrb_value th;
};

typedef mrb_value (*st_func_t)(mrb_state *mrb, struct copy_stream_struct *);

static int
copy_stream_wait_read(struct copy_stream_struct *stp)
{
  printf("copy_stream_wait_read\n");
  FD_ZERO(stp->fds);
  FD_SET(stp->src_fd, stp->fds);
  if (select(stp->src_fd + 1, stp->fds, NULL, NULL, NULL) == -1) {
    stp->syserr = "select";
    stp->error_no = errno;
    return -1;
  }
  return 0;
}

static int
copy_stream_wait_write(struct copy_stream_struct *stp)
{
  printf("copy_stream_wait_write\n");
  FD_ZERO(stp->fds);
  FD_SET(stp->dst_fd, stp->fds);
  if (select(stp->dst_fd + 1, NULL, stp->fds, NULL, NULL) == -1) {
    stp->syserr = "select";
    stp->error_no = errno;
    return -1;
  }
  return 0;
}

static ssize_t
copy_stream_read(
  struct copy_stream_struct *stp,
  char *buf,
  size_t len,
  off_t offset
)
{
  ssize_t ss;
retry_read:
  if (offset == (off_t)-1) {
    ss = read(stp->src_fd, buf, len);
  }
  else {
#ifdef HAVE_PREAD
    ss = pread(stp->src_fd, buf, len, offset);
#else
    stp->notimp = "pread";
    return -1;
#endif
  }
  if (ss == 0) {
    return 0;
  }
  if (ss == -1) {
    switch (errno) {
      case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
      case EWOULDBLOCK:
#endif
        if (copy_stream_wait_read(stp) == -1)
          return -1;
        goto retry_read;
#ifdef ENOSYS
      case ENOSYS:
        stp->notimp = "pread";
        return -1;
#endif
    }
    stp->syserr = offset == (off_t)-1 ?  "read" : "pread";
    stp->error_no = errno;
    return -1;
  }
  return ss;
}

static int
copy_stream_write(struct copy_stream_struct *stp, char *buf, size_t len)
{
  ssize_t ss;
  int off = 0;
  while (len) {
    ss = write(stp->dst_fd, buf+off, len);
    if (ss == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (copy_stream_wait_write(stp) == -1)
          return -1;
        continue;
      }
      stp->syserr = "write";
      stp->error_no = errno;
      return -1;
    }
    off += (int)ss;
    len -= (int)ss;
    stp->total += ss;
  }
  return 0;
}

static void
copy_stream_read_write(mrb_state *mrb, struct copy_stream_struct *stp)
{
  char buf[1024*16];

  off_t copy_length = stp->copy_length;
  mrb_bool use_eof = copy_length == (off_t)-1;
  off_t src_offset = stp->src_offset;
  mrb_bool use_pread = src_offset != (off_t)-1;
  size_t len;
  ssize_t ss;
  mrb_bool use_src_fileno = stp->src_fd != -1;
  mrb_bool use_dst_fileno = stp->dst_fd != -1;
  int flags;

  if (use_src_fileno) {
    flags = fcntl(stp->src_fd, F_GETFL);
    if ((flags & O_ACCMODE) != O_RDONLY && (flags & O_ACCMODE) != O_RDWR) {
      mrb_raise(mrb, mrb_class_get(mrb, "IOError"), "not opened for reading");
    }
  }
  if (use_dst_fileno) {
    flags = fcntl(stp->dst_fd, F_GETFL);
    if ((flags & O_ACCMODE) != O_WRONLY && (flags & O_ACCMODE) != O_RDWR) {
      mrb_raise(mrb, mrb_class_get(mrb, "IOError"), "not opened for writing");
    }
  }

  if (use_pread && !stp->close_src && !use_src_fileno) {
    mrb_funcall(mrb, stp->src, "seek", 2, mrb_fixnum_value(src_offset), mrb_fixnum_value(SEEK_SET));
  }

  while (use_eof || 0 < copy_length) {
    if (!use_eof && copy_length < (off_t)sizeof(buf)) {
      len = (size_t)copy_length;
    }
    else {
      len = sizeof(buf);
    }
    if (use_src_fileno) {
      if (use_pread) {
        ss = copy_stream_read(stp, buf, len, src_offset);
        if (0 < ss)
          src_offset += ss;
      }
      else {
        ss = copy_stream_read(stp, buf, len, (off_t)-1);
      }
      if (ss <= 0) /* EOF or error */
        return;
      if (use_dst_fileno) {
        if (copy_stream_write(stp, buf, ss) < 0)
          return;
      }
      else {
        stp->total += mrb_int(mrb, mrb_funcall(mrb, stp->dst, "write", 1, mrb_str_new_static(mrb, buf, ss)));
      }
    }
    else {
      mrb_value buf_obj = mrb_funcall(mrb, stp->src, "read", 1, mrb_fixnum_value(len));
      if (mrb_nil_p(buf_obj)) /* EOF */
        return;

      if (!mrb_string_p(buf_obj)) /* unexpected spec */
        mrb_raisef(mrb, E_RUNTIME_ERROR, "IO#read should return String or nil, got %S", buf_obj);

      ss = RSTRING_LEN(buf_obj);
      if (ss <= 0) /* EOF or error */
        return;
      if (use_dst_fileno) {
        if (copy_stream_write(stp, RSTRING_PTR(buf_obj), ss) < 0)
          return;
      }
      else {
        stp->total += mrb_int(mrb, mrb_funcall(mrb, stp->dst, "write", 1, buf_obj));
      }
    }

    if (!use_eof)
      copy_length -= ss;
  }
}

#ifdef USE_SENDFILE
/*
-1:error
0:failback
1:success
*/
static int
copy_stream_sendfile(mrb_state *mrb, struct copy_stream_struct *stp)
{
  struct stat src_stat, dst_stat;
  ssize_t ss;
  off_t copy_length = stp->copy_length;
  off_t src_offset = stp->src_offset;
  mrb_bool use_pread = stp->src_offset != (off_t)-1;

  if (stp->src_fd == -1)
    return 0;

  if (stp->dst_fd == -1)
    return 0;

  if (fstat(stp->src_fd, &src_stat) == -1) {
    stp->syserr = "fstat";
    stp->error_no = errno;
    return -1;
  }
  if (!S_ISREG(src_stat.st_mode))
    return 0;

  if (fstat(stp->dst_fd, &dst_stat) == -1) {
    stp->syserr = "fstat";
    stp->error_no = errno;
    return -1;
  }

  if (copy_length == (off_t)-1) {
    if (use_pread)
      copy_length = src_stat.st_size - src_offset;
    else {
      off_t cur;
      errno = 0;
      cur = lseek(stp->src_fd, 0, SEEK_CUR);
      if (cur == (off_t)-1 && errno) {
        stp->syserr = "lseek";
        stp->error_no = errno;
        return -1;
      }
      copy_length = src_stat.st_size - cur;
    }
  }

retry_sendfile:
#if SIZEOF_OFF_T > SIZEOF_SIZE_T
  /* we are limited by the 32-bit ssize_t return value on 32-bit */
  ss = (copy_length > (off_t)SSIZE_MAX) ? SSIZE_MAX : (ssize_t)copy_length;
#else
  ss = (ssize_t)copy_length;
#endif
  if (use_pread) {
    ss = sendfile(stp->dst_fd, stp->src_fd, &src_offset, ss);
  }
  else {
    ss = sendfile(stp->dst_fd, stp->src_fd, NULL, ss);
  }
  if (0 < ss) {
    stp->total += ss;
    copy_length -= ss;
    if (0 < copy_length) {
      goto retry_sendfile;
    }
  }
  if (ss == -1) {
    switch (errno) {
      case EINVAL:
#ifdef ENOSYS
      case ENOSYS:
#endif
        return 0;
    }
    stp->syserr = "sendfile";
    stp->error_no = errno;
    return -1;
  }

  return 1;
}
#endif

static mrb_value
copy_stream_body(mrb_state *mrb, struct copy_stream_struct *stp)
{
  mrb_value src_path = mrb_nil_value();
  mrb_value dst_path = mrb_nil_value();
  int src_fd, dst_fd;
  const int common_oflags = 0
#ifdef O_NOCTTY
  | O_NOCTTY
#endif
#ifdef O_CLOEXEC
  | O_CLOEXEC
#endif
;

  stp->total = 0;
  stp->src_fd = stp->dst_fd = -1;

  if (mrb_string_p(stp->src))
    src_path = stp->src;
  else if (mrb_respond_to(mrb, stp->src, mrb_intern_lit(mrb, "to_path")))
    src_path = mrb_convert_type(mrb, stp->src, MRB_TT_STRING, "String", "to_path");

  if (mrb_string_p(stp->dst))
    dst_path = stp->dst;
  else if (mrb_respond_to(mrb, stp->dst, mrb_intern_lit(mrb, "to_path")))
    dst_path = mrb_convert_type(mrb, stp->dst, MRB_TT_STRING, "String", "to_path");

  if (mrb_nil_p(src_path)) {
    /* optimize */
    mrb_value mayfd = mrb_check_convert_type(mrb, stp->src, MRB_TT_FIXNUM, NULL, "fileno");
    if (mrb_fixnum_p(mayfd)) {
      stp->src_fd = mrb_fixnum(mayfd);
    }
  }
  else {
    src_fd = open(RSTRING_PTR(src_path), O_RDONLY|common_oflags);
    if (src_fd == -1) {
      mrb_sys_fail(mrb, 0);
    }
    stp->src_fd = src_fd;
    stp->close_src = TRUE;
  }

  if (mrb_nil_p(dst_path)) {
    /* optimize */
    mrb_value mayfd = mrb_check_convert_type(mrb, stp->dst, MRB_TT_FIXNUM, NULL, "fileno");
    if (mrb_fixnum_p(mayfd)) {
      stp->dst_fd = mrb_fixnum(mayfd);
    }
  }
  else {
    dst_fd = open(RSTRING_PTR(dst_path), O_WRONLY|O_CREAT|O_TRUNC|common_oflags, 0666);
    if (dst_fd == -1) {
      mrb_sys_fail(mrb, 0);
    }
    stp->dst_fd = dst_fd;
    stp->close_dst = TRUE;
  }

#ifdef USE_SENDFILE
  if (copy_stream_sendfile(mrb, stp) != 0)
    goto finish;
#endif
  copy_stream_read_write(mrb, stp);

#ifdef USE_SENDFILE
  finish:
#endif
  return mrb_fixnum_value(stp->total);
}

static mrb_value
copy_stream_finalize(mrb_state *mrb, struct copy_stream_struct *stp)
{
  if (stp->close_src && close(stp->src_fd) == -1) {
    mrb_sys_fail(mrb, 0);
  }
  if (stp->close_dst && close(stp->dst_fd) == -1) {
    mrb_sys_fail(mrb, 0);
  }
  if (stp->syserr) {
    errno = stp->error_no;
    mrb_sys_fail(mrb, stp->syserr);
  }
  if (stp->notimp) {
    mrb_raisef(mrb, E_NOTIMP_ERROR, "%S() not implemented", mrb_str_new_static(mrb, stp->notimp, strlen(stp->notimp)));
  }
  return mrb_nil_value();
}

static mrb_value
mrb_ensure_copy_stream(
  mrb_state *mrb,
  st_func_t body,
  struct copy_stream_struct *b_data,
  st_func_t ensure,
  struct copy_stream_struct * e_data
)
{
  struct mrb_jmpbuf *prev_jmp = mrb->jmp;
  struct mrb_jmpbuf c_jmp;
  mrb_value result;

  MRB_TRY(&c_jmp) {
    mrb->jmp = &c_jmp;
    result = body(mrb, b_data);
    mrb->jmp = prev_jmp;
  } MRB_CATCH(&c_jmp) {
    mrb->jmp = prev_jmp;
    ensure(mrb, e_data);
    MRB_THROW(mrb->jmp);
  } MRB_END_EXC(&c_jmp);

  ensure(mrb, e_data);
  mrb_gc_protect(mrb, result);
  return result;
}

/*
 *  call-seq:
 *     IO.copy_stream(src, dst)
 *     IO.copy_stream(src, dst, copy_length)
 *     IO.copy_stream(src, dst, copy_length, src_offset)
 *
 *  IO.copy_stream copies <i>src</i> to <i>dst</i>.
 *  <i>src</i> and <i>dst</i> is either a filename or an IO.
 *
 *  This method returns the number of bytes copied.
 *
 *  If optional arguments are not given,
 *  the start position of the copy is
 *  the beginning of the filename or
 *  the current file offset of the IO.
 *  The end position of the copy is the end of file.
 *
 *  If <i>copy_length</i> is given,
 *  No more than <i>copy_length</i> bytes are copied.
 *
 *  If <i>src_offset</i> is given,
 *  it specifies the start position of the copy.
 *
 *  When <i>src_offset</i> is specified and
 *  <i>src</i> is an IO,
 *  IO.copy_stream doesn't move the current file offset.
 *
 */
static mrb_value
io_copy_stream(mrb_state *mrb, mrb_value io)
{
  struct copy_stream_struct st;
  mrb_value copy_length = mrb_nil_value();
  mrb_value src_offset = mrb_nil_value();
  memset(&st, 0, sizeof(struct copy_stream_struct));

  mrb_get_args(mrb, "oo|oo", &st.src, &st.dst, &copy_length, &src_offset);

  if (mrb_nil_p(copy_length))
    st.copy_length = (off_t)-1;
  else
    st.copy_length = mrb_int(mrb, copy_length);

  if (mrb_nil_p(src_offset))
    st.src_offset = (off_t)-1;
  else
    st.src_offset = mrb_int(mrb, src_offset);

  mrb_ensure_copy_stream(mrb, copy_stream_body, &st, copy_stream_finalize, &st);

  return mrb_fixnum_value(st.total);
}

void
mrb_mruby_io_copy_stream_gem_init(mrb_state* mrb)
{
  struct RClass *io = mrb_define_class(mrb, "IO", mrb->object_class);
  mrb_define_class_method(mrb, io, "copy_stream", io_copy_stream, MRB_ARGS_REQ(2));

  mrb_define_class(mrb, "IOError", mrb->eStandardError_class);
}

void
mrb_mruby_io_copy_stream_gem_final(mrb_state* mrb)
{
}
