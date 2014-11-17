# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'rugged/mysql/version'

Gem::Specification.new do |spec|
  spec.name          = 'rugged-mysql'
  spec.version       = Rugged::Mysql::VERSION
  spec.authors       = ['Jitang Zheng']
  spec.email         = ['jitang.zheng@gmail.com']
  spec.summary       = %q{A mysql backend of rugged.}
  spec.description   = %q{Enables rugged to store git objects and references into MySQL.}
  spec.homepage      = 'https://github.com/chonglou/rugged-mysql'
  spec.license       = 'MIT'

  spec.files         = `git ls-files -z`.split("\x0")
  spec.executables   = spec.files.grep(%r{^bin/}) { |f| File.basename(f) }
  spec.test_files    = spec.files.grep(%r{^(test|spec|features)/})
  spec.require_paths = ['lib']

  spec.add_development_dependency 'bundler', '~> 1.7'
  spec.add_development_dependency 'rake', '~> 10.0'
  spec.add_dependency 'rugged'
end
