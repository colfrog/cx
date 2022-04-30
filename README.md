# cx

A directory history management utility written in C

## History of cx

I wrote this a few years ago, and I actually use it very often even though I’ve long forgotten about its code (not that it’s hard to remember), so I thought I’d share it here. It’s actually proven to be very robust for me.

Its purpose is to be a very simple, shell-agnostic clone of [z](https://github.com/ohmyzsh/ohmyzsh/tree/master/plugins/z). I made it on FreeBSD and later modified to work on my Linux installs. The program has POSIX shell bindings, and specific bindings for csh, zsh and fish, which work more closely with the specific shell’s distinctive features. cx attempts to do cli directory management the right way where the right way can be defined, so that it can also be as portable as possible.

The original commit log is long gone in a crumbled mercurial repository on an erased VPS. So this is how I came to make this. I needed to keep a history of visited directories in a way that would work on any shell I used. I used tcsh on FreeBSD and zsh on Linux, I needed my tool to work on both. Where no POSIX shell standard will save us, what better way than to write an overly engineered but very barebones C program with a daemon-client architecture, to reassure yourself behind a few more layers of POSIX?

**UPDATE** No more daemon

## Using cx

* Include the script corresponding to your shell from `/usr/local/share/cx/` in your shell's startup script (.bashrc, .cshrc, .zshrc and the likes)
* Move around with cd to fill the database
* Jump around with cx to known locations, priority is based on frecency (modify `calculate_priority`)
* Non-existent directories are automatically removed from the database, so you don't need to maintain it

## Installing

This program requires the SQLite3 development headers and library.

`make && sudo make install` should build and install the program to /usr/local by default. Defining the `PREFIX` environment variable will let you define where it's installed. Scripts for various shells are installed in `${PREFIX}/share/cx`, which you should include for your respective shell to have convenient access to the cx command.

#### flags

* -D <path>: Specify the database path
* -p <path>: Add an entry representing this path, if it exists
* -i <id>: Specify the ID of the entry instead of looking up
* -d: Dump the database's contents
* -s: Set priority for the entry
* -l: Lock the entry
* -u: Unlock the entry
* -t: Toggle the lock on the entry
* -r: Remove the entry
