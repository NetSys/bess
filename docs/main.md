## BESS Documentation

> *This page is under construction.*

Note that the source code of BESS is under active development, thus many design and implementation details are subject to change. Also, there are some features that are currently missing but planned to be implemented sooner or later. Check out our [TODO list](todo.md).

### Overview
* TODO: [Introduction](intro.md)
* [Installation](install.md)
* [Launching BESS](howtorun.md)
* BESS design and architecture
* How to contribute & coding conventions

### Using BESS for Operators and Control-Plane Developers
* bessctl command-line utility
* BESS configuration files
* libbess-python
* List of drivers
* List of modules

### BESS Internals for Data-Plane Developers
* Master & worker threads
* [Packet buffer (struct snbuf)](snbuf.md)
* [Packet batch](pktbatch.md)
* [Generic data structure for controller interface (snobj)](snobj.md)
* TODO: [Tasks and traffic classes](tc.md)
* Port details
* Writing new port drivers
* [Module details](module_details.md)
* TODO: [Writing new modules](writing_modules.md)
* Logging and debugging
* Performance optimization