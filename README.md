rugged-mysql
============

Enables rugged to store git objects and references into MySQL.


## Installation

Add this line to you application's Gemfile:

    gem 'rugged-mysql', git:git@github.com:chonglou/rugged-mysql.git

And then execute:

    bundle install


## Usage

Create the backend:

    require 'rugged-mysql'
    mysql_backend = Rugged::MySql::Backend.new(host:'localhost', port:3306, username:'git', password:'tig', db:'git')

And pass it to rugged:
    
    repo = Rugged::Repository.bare('repo-name', backend:mysql_backend)

Or

    repo = Rugged::Repository.init_at('repo-name', :bare, backend:mysql_backend)


Each instance of the backend consumes a single MySql connection.

Enjoy it!
