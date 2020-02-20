
#include "libB/libB.h"

#include <iostream>
#include "libA/libA.h"

// new comments in the source code
// this is libB

void hello_libB(int indent, const std::string& msg) {
    std::cout << std::string(indent, ' ') << "libB: " << msg << std::endl;
    std::cout << "libB version 1.0" << std::endl;
    
    hello_libA(indent+1, "called from libB");
}