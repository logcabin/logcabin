#include "identicon.h"

identicon::identicon()
{
    ;
}

// Set the width and height of the identicon
identicon::identicon(int height, int width)
{
    this->height = height;
    this->width = width;
    this->x = (width / 2) - 1;
    this->y = (height / 2) - 1;
    this->initialx = this->x;
    this->initialy = this->y;
}

//Set the width and height of the identicon and 
// the starting coordinates of the identicon
identicon::identicon(int height, int width, int startx, int starty)
{
    this->height = height;
    this->width = width;
    if(startx < width) this->x = startx;
    else this->x = (width / 2) - 1;
    if(starty < height) this->y = starty;
    else this->y = (height / 2) - 1;
    this->initialx = this->x;
    this->initialy = this->y;
}

int identicon::find(string searchstring, char target)
{
    int length = searchstring.length();
    for(int i = 0; i < length; i++)
    {
        if(searchstring[i] == target)
        {
            return i;
        }
        else{
            
        }
    }
    return -1;
}

// Generate the identicon with hash
void identicon::generate(string hash)
{
    char identicon[width][height];
    
    memset(identicon, ' ', sizeof(identicon));
    int length = hash.length();
    int character = -1;
    char temp;
    for(int i = 0; i < length; i++)
    {
        temp = hash[i];
        character = this->find(this->numarray, temp);

        if (character > 15) character -= 6;
        if (character == -1) character = 16;

        identicon[this->x][this->y] = this->chararray[character];

        switch(character % 4)
        {
            case 0:
            if(this->x - 1 >= 0) this->x -= 1;
            if(this->y -1 >= 0) this->y -= 1;
            break;

            case 1:
            if(this->x + 1 <= this->width) this->x += 1;
            if(this->y +1 <= this->height) this->y += 1;
            break;

            case 2:
            if(this->x - 1 >= 0) this->x -= 1;
            if(this->y +1 <= this->height) this->y += 1;
            break;

            case 3:
            if(this->x + 1 <= this->width) this->x += 1;
            if(this->y -1 >= 0) this->y -= 1;
        }
    }

    cout << "  ";
    for(int i = 0; i < width; i++) cout << (i % 10);
    cout << "  " << endl;

    cout << " +";
    for(int i = 0; i < width; i++) cout << '-';
    cout << "+ x" << endl;

    for(int h = 0; h < this->height; h++)
    {
        cout << (h % 10) << "|";
        for(int w = 0; w < this->width; w++)
        {
            cout << identicon[w][h];
        }
        cout << "|" << endl;
    }

    cout << " +";
    for(int i = 0; i < width; i++) cout << '-';
    cout << "+ " << endl;

    cout << " y" << endl;

    this->x = this->initialx;
    this->y = this->initialy;
}