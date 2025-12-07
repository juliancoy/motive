#include <iostream>
#include "fonts.h"

int main() {
    auto bitmap = fonts::renderText("Test", 16);
    std::cout << "Font bitmap width: " << bitmap.width << ", height: " << bitmap.height << std::endl;
    std::cout << "Pixels size: " << bitmap.pixels.size() << std::endl;
    return 0;
}
