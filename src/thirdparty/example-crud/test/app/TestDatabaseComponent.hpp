
#ifndef TEST_DATABASECOMPONENT_HPP
#define TEST_DATABASECOMPONENT_HPP

#include "db/UserDb.hpp"
#include "lwcomm/lwcomm.h"

class TestDatabaseComponent {
public:

  /**
   * Create database connection provider component
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<vsoa::provider::Provider<vsoa::sqlite::Connection>>, dbConnectionProvider)([] {

    /* Create database-specific ConnectionProvider */
    std::string datapath = LWComm::GetDataPath();
    std::string testdata_basefile = datapath + LW_OS_DIR_SEPARATOR + "test_db.sqlite";
    auto connectionProvider = std::make_shared<vsoa::sqlite::ConnectionProvider>(testdata_basefile);

    /* Create database-specific ConnectionPool */
    return vsoa::sqlite::ConnectionPool::createShared(connectionProvider,
                                                       10 /* max-connections */,
                                                       std::chrono::seconds(5) /* connection TTL */);

  }());

  /**
   * Create database client
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<UserDb>, userDb)([] {

    /* Get database ConnectionProvider component */
    VSOA_COMPONENT(std::shared_ptr<vsoa::provider::Provider<vsoa::sqlite::Connection>>, connectionProvider);

    /* Create database-specific Executor */
    auto executor = std::make_shared<vsoa::sqlite::Executor>(connectionProvider);

    /* Create MyClient database client */
    return std::make_shared<UserDb>(executor);

  }());


};

#endif //TEST_DATABASECOMPONENT_HPP
