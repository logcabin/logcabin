#include <gtest/gtest.h>
#include "Identicon/identicon.h"
#include "Identicon/identicon.cpp"

TEST(Identicon, generate){
    identicon test = identicon(20, 20, 26, 2);
    string hash = "fc94b0c1e5b0987c5843997697ee9fb7";
    testing::internal::CaptureStdout();
    test.generate(hash);
    std::string output = testing::internal::GetCapturedStdout();
    std::string tester = "  01234567890123456789  \n +--------------------+ x\n0|        +@#         |\n1|         O@ B       |\n2|        *SX@ ^      |\n3|         &  ^ O     |\n4|           @ &      |\n5|            S       |\n6|                    |\n7|                    |\n8|                    |\n9|                    |\n0|                    |\n1|                    |\n2|                    |\n3|                    |\n4|                    |\n5|                    |\n6|                    |\n7|                    |\n8|                    |\n9|                    |\n +--------------------+ \n y\n";
    EXPECT_PRED2([](auto str1, auto str2){
        return str1 == str2;}, output, tester);
}

int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}