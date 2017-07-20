# How To Contribute
Thank you for your interest in BESS!  We welcome new contributions.

## Contributor License Agreement
We ask that all contributors agree to a contributor license agreement (CLA); all new contributions should be under a BSD3 license (as the core of BESS is).  We use CLAHub for signing CLAs; to get started, please visit [CLAHub for BESS here](https://www.clahub.com/agreements/NetSys/bess).

## Sending Patches
You are welcome to [make a GitHub Pull Request](https://github.com/NetSys/bess/pulls) (PR) for new features and bug fixes.  All PRs will be reviewed to maintain high code quality. Everyone is welcome to join the process of reviewing code. Please understand that we may ask for further changes to your PRs to address any errors, coding style issues, etc.

### Coding Style
Please respect the following coding styles. Let's not be too dogmatic, though.

* C++: [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
  * There is a [`.clang_format`](https://github.com/NetSys/bess/blob/master/core/.clang-format) file that you can utilize directly with [`clang-format`](https://clang.llvm.org/docs/ClangFormat.html) or integrate with your favorite editor ([Vim](https://github.com/rhysd/vim-clang-format), [Emacs](https://llvm.org/svn/llvm-project/cfe/trunk/tools/clang-format/clang-format.el), [Atom](https://atom.io/packages/clang-format), etc.)
* C: [Linux kernel coding style](https://github.com/torvalds/linux/blob/master/Documentation/process/coding-style.rst)
  * Currently C is only used for the Linux kernel module.
* Python: [PEP 8 -- Style Guide for Python Code](https://www.python.org/dev/peps/pep-0008/)
  * For new code, please make it compatible with both Python 2 and 3.

### Running Tests
For C++ and Python code updates, we recommend adding unit tests with [Google Test](https://github.com/google/googletest) and [unittest.py](https://docs.python.org/2/library/unittest.html). Also, please run existing tests to make sure your changes to avoid regressions.

## Questions? Found a Bug? Have a Request for New Features?
Great. You can use GitHub Issues for any questions, suggestions, or issues. Please do not email individuals.

## Contributions to the Wiki
[The GitHub Wiki](https://github.com/NetSys/bess/wiki) is open to everyone for edit. Feel free to add any changes, it can be a big help for others!

# List of Contributors
Please add your name to the end of this file and include this file to the PR, unless you want to remain anonymous.

* Sangjin Han
* Keon Jang
* Aurojit Panda
* Saikrishna Edupuganti
* Xiaoshuang Wang
* Shinae Woo
* He Peng
* Joshua Reich
* Brian Kim
* Murphy McCauley
* Amin Tootoonchian
* Chang Lan
* Melvin Walls
* Barath Raghavan
* Justine Sherry
* Chaitanya Lala
* Tony Situ
* Vivian Fang
* Joshua Stone
* Daniele di Proietto
* Felicián Németh
* James Murphy
* Steven H. Wang
* Gal Sagie
* Chris Torek
* Eran Gampel
