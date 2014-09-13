cxx-tty-view-sample
===================

This program requires terminal (tty).

Sample
------

    $ make
    cc -Wall -pedantic-errors   -c -o prog.o prog.c
    cc -ltermcap  prog.o   -o prog
    $ ./prog
    waiting for input (1) ...
    waiting for input (2) ...
    waiting for input (3) ...
    input.1> test
    waiting for input (4) ...
    input.2> ^D

"waiting" messages are printed periodically WITHOUT mixed with input line.
