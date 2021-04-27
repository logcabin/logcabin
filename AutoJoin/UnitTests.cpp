#include <gtest/gtest.h>
#include "autojoin.cpp"

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

TEST(parseString, testing){
    autojoin TestObj;
    std::vector<string> TestVect = TestObj.parseString("127.0.0.1:1552", ':');
    string TestStr[]={"127.0.0.1","1552"};
    for( int i=0; i<TestVect.size();i++){
       EXPECT_EQ(TestVect.at(i),TestStr[i]);
   }
}

TEST(parseString2, testing){
   autojoin TestObj;
    std::vector<string> TestVect = TestObj.parseString("test:1:123:Test", ':');
    string TestStr[]={"test","1","123","Test"};
    
    for( int i=0; i<TestVect.size();i++){
       EXPECT_EQ(TestVect.at(i),TestStr[i]);
   }
}

TEST(parseString3, testing){
   autojoin TestObj;
    std::vector<string> TestVect = TestObj.parseString("Test", '.'); 
    
    for( int i=0; i<TestVect.size();i++){
       EXPECT_EQ(TestVect.at(i),"Test");
   }
}

int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}