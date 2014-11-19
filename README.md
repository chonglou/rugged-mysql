rugged-mysql
============

Enables rugged(libgit2 bindings in Ruby, visit by https://github.com/libgit2/rugged) to store git objects and references into MySQL.



## Installation

Add this line to you application's Gemfile:

    gem 'rugged-mysql'
Or:

    gem 'rugged-mysql', chonglou/rugged-mysql

And then execute:

    bundle install


## Usage

Create the backend:

    require 'rugged-mysql'
    mysql_backend = Rugged::MySql::Backend.new(host:'localhost', port:3306, username:'git', password:'tig', database:'git')

And pass it to rugged:
    
    repo = Rugged::Repository.bare('repo-name', backend:mysql_backend)

Or

    repo = Rugged::Repository.init_at('repo-name', :bare, backend:mysql_backend)


Each instance of the backend consumes a single MySql connection.

Enjoy it!

## Contributing
Fork chonglou/rugged-mysql on GitHub, make it awesomer (preferably in a branch named for the topic), send a pull request.
