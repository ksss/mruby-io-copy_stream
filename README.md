mruby-io-copy_stream
=====

The implementation of `IO.copy_stream` method for mruby.

# Feature

- Support `pread(2)`
- Support `sendfile(2)` if linux

# Special spec of this library

- Use fd for system call if respond to `fileno` method.
- Use `read` and `write` method if doesn't have fd.
- Use `seek` method if doesn't have fd for src_offset option.
- It doesn't have dependency to IO library.

# Performance

```rb
# bigfile is 440MB text file

t = Time.now
IO.copy_stream("bigfile", "bigfile-copy")
puts Time.now - t

t = Time.now
File.open("bigfile") do |src|
  IO.copy_stream(src, "bigfile-copy")
end
puts Time.now - t

t = Time.now
File.open("bigfile-copy", "w") do |dst|
  IO.copy_stream("bigfile", dst)
end
puts Time.now - t

t = Time.now
File.open("bigfile") do |src|
  File.open("bigfile-copy", "w") do |dst|
    IO.copy_stream(src, dst)
  end
end
puts Time.now - t
```

CRuby

```
$ ruby t.rb
0.407673
0.400015
0.395681
0.401305
```

mruby use this library with [mruby-io](https://github.com/iij/mruby-io)

```
$ mruby t.rb
0.452422
0.419008
0.403466
0.412026
```

# How to development with docker

```
$ docker-compose -f docker/docker-compose.yml build
$ docker-compose -f docker/docker-compose.yml run test
```

```
$ make test
```

# See also

https://ruby-doc.org/core-2.4.1/IO.html#method-c-copy_stream
