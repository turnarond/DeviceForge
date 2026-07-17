
#include "AppComponent.hpp"

#include "controller/UserController.hpp"
#include "controller/StaticController.hpp"

#include "oatpp-swagger/Controller.hpp"

#include "oatpp/network/Server.hpp"

#include <iostream>

void run() {
  
  AppComponent components; // Create scope Environment components
  
  /* Get router component */
  VSOA_COMPONENT(std::shared_ptr<vsoa::web::server::HttpRouter>, router);

  vsoa::web::server::api::Endpoints docEndpoints;

  docEndpoints.append(router->addController(UserController::createShared())->getEndpoints());

  router->addController(vsoa::swagger::Controller::createShared(docEndpoints));
  router->addController(StaticController::createShared());

  /* Get connection handler component */
  VSOA_COMPONENT(std::shared_ptr<vsoa::network::ConnectionHandler>, connectionHandler);

  /* Get connection provider component */
  VSOA_COMPONENT(std::shared_ptr<vsoa::network::ServerConnectionProvider>, connectionProvider);

  /* create server */
  vsoa::network::Server server(connectionProvider,
                                connectionHandler);
  
  VSOA_LOGD("Server", "Running on port %s...", connectionProvider->getProperty("port").toString()->c_str());
  
  server.run();

  /* stop db connection pool */
  VSOA_COMPONENT(std::shared_ptr<vsoa::provider::Provider<vsoa::sqlite::Connection>>, dbConnectionProvider);
  dbConnectionProvider->stop();
  
}

/**
 *  main
 */
int main(int argc, const char * argv[]) {

  vsoa::base::Environment::init();

  run();
  
  /* Print how many objects were created during app running, and what have left-probably leaked */
  /* Disable object counting for release builds using '-D OATPP_DISABLE_ENV_OBJECT_COUNTERS' flag for better performance */
  std::cout << "\nEnvironment:\n";
  std::cout << "objectsCount = " << vsoa::base::Environment::getObjectsCount() << "\n";
  std::cout << "objectsCreated = " << vsoa::base::Environment::getObjectsCreated() << "\n\n";
  
  vsoa::base::Environment::destroy();
  
  return 0;
}
