#ifndef UserControllerTest_hpp
#define UserControllerTest_hpp

#include "oatpp-test/UnitTest.hpp"

class UserControllerTest : public vsoa::test::UnitTest {
public:
    UserControllerTest() : vsoa::test::UnitTest("TEST[UserControllerTest]")
    {}

    void onRun() override;
};

#endif // UserControllerTest_hpp
