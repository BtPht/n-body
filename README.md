# n-body
N-body simulation using Raylib

Build
-----

The only dependency is to Raylib for drawing.

#### Raylib as a local repository

This is the easiest way to easily build against the latest version abvailable or a specific tag.

Download and build [Raylib](https://github.com/raysan5/raylib) then update the references at the top of `main.c` and in the `Makefile`.

#### Raylib as a system library

If you installed Raylib as a system library then replace the relative imports in `main.c` and in the Makefile. 


Notes and ideas
---------------

* The code has been optimized to run with a lot of bodies, making the code harder to read so a reference implementation using the standard functions and formulas is provided.
* The execution is multi-threaded evenly, all the threads will process 1/M of the calculations where M is the number of threads.
* Drawing has been optimized and should not be a bottleneck, drawing is done in the main thread but doesn't need to be accounted for when chosing the number of worker threads.
* In the current code the speeds and accelerations are 0 at the start so all the bodies collapse. It is possible to give some initial speed to get completely different results : one could calculate a speed vector going perpendicular that would excatly compensate the acceleration vector making all the bodies rotate around the theoretical center of gravity.
* The initial placement is also important, if the bodies are randomly places in a square area the corners will collapse slower creating cluster artifacts.
* The fast implemenation completely gets rid of the `sqrt` call since we only need the square of the distance.
* In this implementation the distance between two objects easily gets below 1 creating acceleration (there's some physical law against that) so instead any sub 1 distance is raised to 1 and the speed decreased slightly creating some friction.
* All the calculations are done per dimension so it would be trivial to extend the code to 3 or more.

