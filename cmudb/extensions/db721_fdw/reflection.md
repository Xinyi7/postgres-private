Strengths:
The data formats are fairly easy to extend, for defining custom data formats for tables. The routines are quite straightforward on how things are separated. In addition, the predicates and data passed in can be obtained while we are scanning the table, which give the programmers much freedom to optimize and change the data formats for better efficiency. In addition, I like the fact that they have a c++ extension.

Weaknesses:
The documentation is really hard to read. Especially with where to get data and how functions should be used. I encountered multiple situations where I guessed from the function name and parameters what it does and it turns out that it can’t do that. At the same time, there isn’t really a good place to find how every function is supposed to be used and how to get the data we wish to use. The documentation was also very vague about what is allowed in c++ and what isn’t. For example, smart pointers are not allowed in objects and structs but sometimes it seems ok to use it inside functions.

Suggestions for improving the file format:
Better documentation please! Maybe it could be a future project to write an tutorial blog for where everything is in postgres, with example usages.

Feedback on this project:
I like this project. Not too easy and not too difficult. One small thing though: the alignment in the starter code is a little strange. Perhaps use tap for indent instead of two  spaces? Perhaps for future references, some edge cases can be tested?
