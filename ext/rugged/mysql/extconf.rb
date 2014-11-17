require 'mkmf'
require 'rugged'

$CFLAGS << " #{ENV['CFLAGS']}"
$CFLAGS << ' -g'
$CFLAGS << ' -O3' unless $CFLAGS[/-O\d/]
$CFLAGS << ' -Wall -Wno-comment'

unless find_executable('cmake')
  abort 'ERROR: CMake is required to build Rugged.'
end

MAKE = find_executable('gmake') || find_executable('make')
unless MAKE
  abort 'ERROR: GNU make is required to build Rugged.'
end

create_makefile('rugged/redis/rugged_mysql')

