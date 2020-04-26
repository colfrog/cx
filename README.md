# cx

A directory history utility written in C

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

cxd is a daemon that runs in the background and receives messages from a UNIX socket at `~/.cx/socket` by default. A simple text-based protocol is defined to send and receive messages.

#### flags

* -d: daemonize
* -D <path>: define the database path
* -s <path>: define the socket path

### cxc

cxc is the client, it basically figures out what you want to do from your arguments, and sends the message to cxd. It's used as a backend for the cx command, but it can also be used to manually change a directory's priority, lock or unlock priorities in place, remove entries and push new entries. You can also use it to dump the entire database's contents.

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
