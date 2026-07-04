#include "UserControllerTest.hpp"

#include "oatpp/web/client/HttpRequestExecutor.hpp"
#include "oatpp-test/web/ClientServerTestRunner.hpp"

#include "controller/UserController.hpp"
#include "lwcomm/lwcomm.h"
#include "app/TestClient.hpp"
#include "app/TestComponent.hpp"

#include <cstdio>

void UserControllerTest::onRun() {

  LWComm::GetDataPath();
  std::string datapath = LWComm::GetDataPath();
  std::string testdata_basefile =datapath + LW_OS_DIR_SEPARATOR + "test_db.sqlite";
  /* Remove test database file before running the test */
  VSOA_LOGI(TAG, "DB-File='%s'", testdata_basefile.c_str());
  std::remove(testdata_basefile.c_str());

  /* Register test components */
  TestComponent component;

  /* Create client-server test runner */
  vsoa::test::web::ClientServerTestRunner runner;

  /* Add UserController endpoints to the router of the test server */
  runner.addController(std::make_shared<UserController>());

  /* Run test */
  runner.run([this, &runner] {

    /* Get client connection provider for Api Client */
    VSOA_COMPONENT(std::shared_ptr<vsoa::network::ClientConnectionProvider>, clientConnectionProvider);

    /* Get object mapper component */
    VSOA_COMPONENT(std::shared_ptr<vsoa::data::mapping::ObjectMapper>, objectMapper);

    /* Create http request executor for Api Client */
    auto requestExecutor = vsoa::web::client::HttpRequestExecutor::createShared(clientConnectionProvider);

    /* Create Test API client */
    auto client = TestClient::createShared(requestExecutor, objectMapper);

    auto dto = UserDto::createShared();

    dto->userName = "jondoe";
    dto->email = "jon.doe@abc.com";
    dto->password = "1234";

    /* Call server API */
    auto addedUserResponse = client->addUser(dto);

    /* Assert that server responds with 200 */
    VSOA_ASSERT(addedUserResponse->getStatusCode() == 200);

    /* Read response body as MessageDto */
    auto addedUserDto = addedUserResponse->readBodyToDto<vsoa::Object<UserDto>>(objectMapper.get());

    int addedUserId = addedUserDto->id;

    /* Assert that user has been added */
    auto newUserResponse = client->getUser(addedUserId);

    VSOA_ASSERT(newUserResponse->getStatusCode() == 200);

    auto newUserDto = newUserResponse->readBodyToDto<vsoa::Object<UserDto>>(objectMapper.get());

    VSOA_ASSERT(newUserDto->id == addedUserId);

    /* Delete newly added users */
    auto deletedUserResponse = client->deleteUser(addedUserId);

    VSOA_ASSERT(deletedUserResponse->getStatusCode() == 200);

  }, std::chrono::minutes(10) /* test timeout */);

  /* wait all server threads finished */
  std::this_thread::sleep_for(std::chrono::seconds(1));

  /* stop db connection pool */
  VSOA_COMPONENT(std::shared_ptr<vsoa::provider::Provider<vsoa::sqlite::Connection>>, dbConnectionProvider);
  dbConnectionProvider->stop();

}
