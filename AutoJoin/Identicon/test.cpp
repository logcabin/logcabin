#include "identicon.cpp"

int main(void)
{
    identicon test = identicon(20, 20, 26, 2);
    string hash = "fc94b0c1e5b0987c5843997697ee9fb7";
    cout << "Hash: " << hash << endl;
    test.generate(hash);
}