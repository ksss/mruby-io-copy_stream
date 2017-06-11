self.class.prepend Module.new {
  def assert(*)
    @before.call if @before
    super
    @after.call if @after
  end

  def before(&block)
    @before = block
  end

  def after(&block)
    @after = block
  end
}

class MinumumStringIO
  def initialize(strings = "")
    @strings = strings
    @pos = 0
  end

  def read(len = nil, buf = "")
    b = if len.nil?
      @string[@pos..-1]
    else
      @strings[@pos, len]
    end
    @pos += b.length
    buf.replace(b)
  end

  def write(buf)
    @strings[@pos] = buf
    @pos += buf.length
    buf.length
  end

  def strings
    @strings
  end
end

str = "Hello, World!\n" * 10

before do
  File.open("./tmpfile", "w") do |f|
    f.write(str)
  end
end

assert("IO.copy_stream file to file") do
  assert_equal str.length, IO.copy_stream("./tmpfile", "./out")
  assert_equal str, File.open("./out", "r") { |f| f.read }
end

assert("IO.copy_stream io to file") do
  File.open("./tmpfile") do |src|
    assert_equal str.length, IO.copy_stream(src, "./out")
  end
  assert_equal str, File.open("./out", "r") { |f| f.read }
end

assert("IO.copy_stream file to io") do
  File.open("./out", "w") do |dst|
    assert_equal str.length, IO.copy_stream("./tmpfile", dst)
  end
  assert_equal str, File.open("./out", "r") { |f| f.read }
end

assert("IO.copy_stream io to io") do
  File.open("./tmpfile") do |src|
    File.open("./out", "w") do |dst|
      assert_equal str.length, IO.copy_stream(src, dst)
      assert_equal "", src.read
      assert_equal str.length, dst.pos
      assert_equal 0, IO.copy_stream(src, dst)
    end
  end
  assert_equal str, File.open("./out", "r") { |f| f.read }
end

assert("IO.copy_stream copy_length") do
  assert_equal 10, IO.copy_stream("./tmpfile", "./out", 10)
  assert_equal str[0, 10], File.open("./out", "r") { |f| f.read }

  File.open("./tmpfile") do |src|
    assert_equal 11, IO.copy_stream(src, "./out", 11)
  end
  assert_equal str[0, 11], File.open("./out", "r") { |f| f.read }

  File.open("./out", "w") do |dst|
    assert_equal 12, IO.copy_stream("./tmpfile", dst, 12)
  end
  assert_equal str[0, 12], File.open("./out", "r") { |f| f.read }

  File.open("./tmpfile") do |src|
    File.open("./out", "w") do |dst|
      assert_equal 13, IO.copy_stream(src, dst, 13)
    end
  end
  assert_equal str[0, 13], File.open("./out", "r") { |f| f.read }
end

assert("IO.copy_stream src_offset") do
  assert_equal str.length - 3, IO.copy_stream("./tmpfile", "./out", nil, 3)
  assert_equal str[3..-1], File.open("./out", "r") { |f| f.read }

  assert_equal 3, IO.copy_stream("./tmpfile", "./out", 3, 3)
  assert_equal str[3, 3], File.open("./out", "r") { |f| f.read }

  File.open("./tmpfile") do |src|
    assert_equal str.length - 3, IO.copy_stream(src, "./out", nil, 3)
  end
  assert_equal str[3..-1], File.open("./out", "r") { |f| f.read }

  File.open("./tmpfile") do |src|
    assert_equal 3, IO.copy_stream(src, "./out", 3, 3)
  end
  assert_equal str[3, 3], File.open("./out", "r") { |f| f.read }

  File.open("./out", "w") do |dst|
    assert_equal str.length - 3, IO.copy_stream("./tmpfile", dst, nil, 3)
  end
  assert_equal str[3..-1], File.open("./out", "r") { |f| f.read }

  File.open("./out", "w") do |dst|
    assert_equal 3, IO.copy_stream("./tmpfile", dst, 3, 3)
  end
  assert_equal str[3, 3], File.open("./out", "r") { |f| f.read }

  File.open("./tmpfile") do |src|
    File.open("./out", "w") do |dst|
      assert_equal str.length - 3, IO.copy_stream(src, dst, nil, 3)
    end
  end
  assert_equal str[3..-1], File.open("./out", "r") { |f| f.read }

  File.open("./tmpfile") do |src|
    File.open("./out", "w") do |dst|
      assert_equal 3, IO.copy_stream(src, dst, 3, 3)
    end
  end
  assert_equal str[3, 3], File.open("./out", "r") { |f| f.read }

  o = Object.new
  def o.read(len, buf = "")
    @count ||= 0
    @count += 1
    if @count == 1
      buf.replace "hello"
    else
      buf
    end
  end
  assert_equal 5, IO.copy_stream(o, "./out")
  assert_equal "hello", File.open("./out", "r") { |f| f.read }

  # CRuby raise `ArgumentError`
  # But mruby's IO is not standard Object
  # I decided call `seek` method directly
  # This library doesn't dependent on any IO library
  assert_raise(NoMethodError) { IO.copy_stream(o, "./out", 1, 1) }
end

assert("IO.copy_stream StringIO to path") do
  src = MinumumStringIO.new(str)
  assert_equal str.length, IO.copy_stream(src, "./out")
  assert_equal str, File.open("./out", "r") { |f| f.read }
end

assert("IO.copy_stream path to StringIO") do
  dst = MinumumStringIO.new
  assert_equal str.length, IO.copy_stream("./tmpfile", dst)
  assert_equal str, dst.strings
end

assert("IO.copy_stream StringIO to StringIO") do
  src = MinumumStringIO.new(str)
  dst = MinumumStringIO.new
  assert_equal str.length, IO.copy_stream(src, dst)
  assert_equal str, dst.strings
end

assert("IO.copy_stream IO to StringIO") do
  dst = MinumumStringIO.new
  File.open("./tmpfile") do |src|
    assert_equal str.length, IO.copy_stream(src, dst)
  end
  assert_equal str, dst.strings
end

assert("IO.copy_stream StringIO to IO") do
  src = MinumumStringIO.new(str)
  File.open("./out", "w") do |dst|
    assert_equal str.length, IO.copy_stream(src, dst)
  end
  assert_equal str, File.open("./out", "r") { |f| f.read }
end

assert("IO.copy_stream raise Errno::ENOENT when file nothing") do
  assert_raise(Errno::ENOENT) { IO.copy_stream("./nothing", "./out") }
end

assert("IO.copy_stream raise Errno::ENOENT when file nothing") do
  assert_raise(Errno::ENOENT) { IO.copy_stream("./nothing", "./out") }
end
