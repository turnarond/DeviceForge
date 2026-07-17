
#include "oatpp-test/UnitTest.hpp"
#include "vsoa_dto/core/base/Environment.hpp"
#include "UserControllerTest.hpp"

#include <iostream>

namespace {

void runTests() {

  OATPP_RUN_TEST(UserControllerTest);

}

}

int main() {

  vsoa::base::Environment::init();

  runTests();

  /* Print how much objects were created during app running, and what have left-probably leaked */
  /* Disable object counting for release builds using '-D OATPP_DISABLE_ENV_OBJECT_COUNTERS' flag for better performance */
  std::cout << "\nEnvironment:\n";
  std::cout << "objectsCount = " << vsoa::base::Environment::getObjectsCount() << "\n";
  std::cout << "objectsCreated = " << vsoa::base::Environment::getObjectsCreated() << "\n\n";

  VSOA_ASSERT(vsoa::base::Environment::getObjectsCount() == 0);

  vsoa::base::Environment::destroy();

  return 0;
}
