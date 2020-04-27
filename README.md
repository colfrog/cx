# cx

A directory history management utility written in C

## History of cx

I wrote this a few years ago, and I actually use it very often even though I’ve long forgotten about its code (not that it’s hard to remember), so I thought I’d share it here. It’s actually proven to be very robust for me.

Its purpose is to be a very simple, shell-agnostic clone of [z](https://github.com/ohmyzsh/ohmyzsh/tree/master/plugins/z). I made it on FreeBSD and later modified to work on my Linux installs. The program has POSIX shell bindings, and specific bindings for csh, zsh and fish, which work more closely with the specific shell’s distinctive features. cx attempts to do cli directory management the right way where the right way can be defined, so that it can also be as portable as possible.

The original commit log is long gone in a crumbled mercurial repository on an erased VPS. So this is how I came to make this. I needed to keep a history of visited directories in a way that would work on any shell I used. I used tcsh on FreeBSD and zsh on Linux, I needed my tool to work on both. Where no POSIX shell standard will save us, what better way than to write an overly engineered but very barebones C program with a daemon-client architecture, to reassure yourself behind a few more layers of POSIX?

## Using cx

* Start the daemon with `cxd -d` (You might want to add this to your .login or .profile)
* Include the script corresponding to your shell from `/usr/local/share/cx/` in your shell's startup script (.bashrc, .cshrc, .zshrc and the likes)
* Move around with cd to fill the database
* Jump around with cx to known locations, priority is based on frecency
* Non-existent directories are automatically removed from the database, so you don't need to maintain it

## Installing

This program requires the SQLite3 development headers and library.

`make && sudo make install` should build and install the program to /usr/local by default. Defining the `PREFIX` environment variable will let you define where it's installed. Scripts for various shells are installed in `${PREFIX}/share/cx`, which you should include for your respective shell to have convenient access to the cx command.

## How it works

### cxd

cxd is a daemon that runs in the background and receives messages from a UNIX socket at `~/.cx/socket` by default. It also maintains a database at `~/.cx/data` and has a lockfile at `~/.cx/lock` to prevent more than one daemon from running at once. cxd keeps its paths organized by frecency through database operations in reaction to messages that it gets from its socket. A simple text-based protocol is defined for these database operations.

#### flags

* -d: daemonize
* -D <path>: define the database path
* -s <path>: define the socket path

### cxc

cxc is the client, it basically figures out what you want to do from your arguments, and sends the message to cxd. It's used as a back-end for the cx command, but it can also be used to manually change a directory's priority, lock or unlock priorities in place, remove entries and push new entries. You can also use it to dump the entire database's contents.

#### flags

* Any string after the arguments (or after --) is jumped to if a match is found
* -p <path>: add an entry representing this path, if it exists
* -s <path>: specify the path of the socket to use to communicate with cxd
* -d: Dump the database's contents
* -i <id>: specify the ID of the entry to modify using other flags (all flags below require this argument)
* -S: set priority for the entry
* -l: lock the entry
* -u: unlock the entry
* -t: toggle the lock on the entry
* -r: remove the entry
