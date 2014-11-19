require 'mkmf'


$CFLAGS << " #{ENV['CFLAGS']}"
$CFLAGS << ' -I/usr/include/mysql'

RUGGED_EXT_DIR = Gem::Specification.find_by_name('rugged').gem_dir
puts "Using rugged headers from #{RUGGED_EXT_DIR}\n"
$CFLAGS << " -I#{RUGGED_EXT_DIR}/ext/rugged"
$CFLAGS << " -I#{RUGGED_EXT_DIR}/vendor/libgit2/include"
$CFLAGS << " -I#{RUGGED_EXT_DIR}/vendor/libgit2/src"

$CFLAGS << ' -g'
$CFLAGS << ' -O3' unless $CFLAGS[/-O\d/]
$CFLAGS << ' -Wall -Wno-comment -Wno-sizeof-pointer-memaccess'


MAKE = find_executable('gmake') || find_executable('make')
unless MAKE
  abort 'ERROR: GNU make is required to build Rugged.'
end

create_makefile('rugged/redis/rugged-mysql')

