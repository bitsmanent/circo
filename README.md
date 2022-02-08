Hello
=====
circo is a small IRC client for the terminal. It provides a list of views
(buffers) to differentiate between channels, users, and other messages. Each
buffer has its own input text while other relevant informations such as server
and nickname are always visible at the top.

Despite it's minimal design, circo is definitely not a suckless piece of code.
It don't even follows in all respect the UNIX philosophy but it provides a very
power bridge to the IRC servers with a such small code base. In this regard
circo is unique.

Altough it's compactness, circo is a powerful tool with almost all the features
one would expect from an IRC client for the console:

- Per-channel/query buffers
- Command line
- Infinite scrolling
- Resize handling
- Command history
- Raw IRC commands
- Colors support
- Tab completion

None of the CTCP specification has been (nor will be) implemented, which means
no DCC at all. In other words: direct chat and files sending are not available.

Status
======
Some refactor and cleanups are needed but it's almost ready for the 0.1. UTF-8
is not currently supported and I'm not sure it will ever be.

The circo IRC client is actively developed and I'm having a lot of fun in
writing it. If you'll also find yourself having fun with circo, please consider
submitting a pull request.
