MRuby::Build.new do |conf|
  toolchain :gcc
  enable_debug
  conf.gembox 'default'
end
MRuby::Build.new('test') do |conf|
  toolchain :gcc
  enable_debug
  conf.gembox 'full-core'
  conf.enable_bintest
  conf.enable_test
  conf.gem "/mruby-io-copy_stream"
end
