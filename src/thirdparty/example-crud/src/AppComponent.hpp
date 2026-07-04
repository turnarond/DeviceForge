
#ifndef AppComponent_hpp
#define AppComponent_hpp

#include "SwaggerComponent.hpp"
#include "DatabaseComponent.hpp"

#include "ErrorHandler.hpp"

#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/HttpRouter.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"

#include "vsoa_dto/parser/json/mapping/ObjectMapper.hpp"

#include "oatpp/core/macro/component.hpp"

/**
 *  Class which creates and holds Application components and registers components in vsoa::base::Environment
 *  Order of components initialization is from top to bottom
 */
class AppComponent {
public:
  
  /**
   *  Swagger component
   */
  SwaggerComponent swaggerComponent;

  /**
   * Database component
   */
  DatabaseComponent databaseComponent;

  /**
   * Create ObjectMapper component to serialize/deserialize DTOs in Controller's API
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<vsoa::data::mapping::ObjectMapper>, apiObjectMapper)([] {
    auto objectMapper = vsoa::parser::json::mapping::ObjectMapper::createShared();
    objectMapper->getDeserializer()->getConfig()->allowUnknownFields = false;
    return objectMapper;
  }());
  
  /**
   *  Create ConnectionProvider component which listens on the port
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<vsoa::network::ServerConnectionProvider>, serverConnectionProvider)([] {
    return vsoa::network::tcp::server::ConnectionProvider::createShared({"0.0.0.0", 8000, vsoa::network::Address::IP_4});
  }());
  
  /**
   *  Create Router component
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<vsoa::web::server::HttpRouter>, httpRouter)([] {
    return vsoa::web::server::HttpRouter::createShared();
  }());
  
  /**
   *  Create ConnectionHandler component which uses Router component to route requests
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<vsoa::network::ConnectionHandler>, serverConnectionHandler)([] {

    VSOA_COMPONENT(std::shared_ptr<vsoa::web::server::HttpRouter>, router); // get Router component
    VSOA_COMPONENT(std::shared_ptr<vsoa::data::mapping::ObjectMapper>, objectMapper); // get ObjectMapper component

    auto connectionHandler = vsoa::web::server::HttpConnectionHandler::createShared(router);
    connectionHandler->setErrorHandler(std::make_shared<ErrorHandler>(objectMapper));
    return connectionHandler;

  }());

};

#endif /* AppComponent_hpp */
