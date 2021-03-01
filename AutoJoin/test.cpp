#include "autojoin.cpp"

int main(void)
{
    autojoin testobject = autojoin();

    vector<string> test = testobject.parseString("127.0.0.1:1552", ':');

    for(int index = 0; index < (int)test.size(); index++)
    {
        cout << test[index] << endl;
    }
}