#include <string>
#include <iostream>
#include "string.h"
#include <string>

using namespace std;

class identicon {
    private:
    int width = 16;
    int height = 9;
    int x = 8;
    int y = 4;
    int initialx = 8;
    int initialy = 4;
    string numarray = "0123456789ABCDEFabcdef!";
    string chararray = " .o+=*BOX@%&#/^SE";
    int find(string searchstring, char target);
    public:
    identicon();
    identicon(int height, int width);
    identicon(int height, int width, int startx, int starty);
    void generate(string hash);
};