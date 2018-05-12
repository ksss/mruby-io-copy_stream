MRuby::Gem::Specification.new('mruby-io-copy_stream') do |spec|
  spec.license = 'MIT'
  spec.author  = 'ksss <co000ri@gmail.com>'

  spec.add_test_dependency("mruby-errno")
  spec.add_test_dependency("mruby-error")

  env = {
    'CC' => "#{build.cc.command} #{build.cc.flags.join(' ')}",
    'CXX' => "#{build.cxx.command} #{build.cxx.flags.join(' ')}",
    'LD' => "#{build.linker.command} #{build.linker.flags.join(' ')}",
    'AR' => build.archiver.command
  }
  config = "#{build_dir}/config.h"

  file config do
    FileUtils.mkdir_p build_dir, :verbose => true
    Dir.chdir build_dir do
      if ENV['OS'] == 'Windows_NT'
        _pp 'on Windows', dir
        FileUtils.touch "#{build_dir}/config.h", :verbose => true
      else
        _pp './configure', dir
        system env, "#{dir}/configure"
      end
    end
  end
  file "#{dir}/src/io-copy_stream.c" => config
  task :clean do
    FileUtils.rm_f config, :verbose => true
  end

  cc.include_paths << build_dir
end
