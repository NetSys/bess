### How to generaate BESS code documentation

* Install doxygen (sudo apt-get install doxygen)
* cd bess/doxygen
* doxygen bess.dox
* This will create three files:
  * doxygen.errors (usually a list of all undocumented code, all errors go here.)
  * html/ a directory with BESS documentation in HTML
  * latex/ a directory with BESS documentation formatted in LaTeX
