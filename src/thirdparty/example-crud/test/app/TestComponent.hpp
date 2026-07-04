#ifndef TestComponent_hpp
#define TestComponent_hpp

#include "oatpp/web/server/HttpConnectionHandler.hpp"

#include "oatpp/network/virtual_/client/ConnectionProvider.hpp"
#include "oatpp/network/virtual_/server/ConnectionProvider.hpp"
#include "oatpp/network/virtual_/Interface.hpp"

#include "vsoa_dto/parser/json/mapping/ObjectMapper.hpp"

#include "oatpp/core/macro/component.hpp"

#include "TestDatabaseComponent.hpp"

/**
 * Test Components config
 */
class TestComponent {
public:

  TestDatabaseComponent databaseComponent;

  /**
   * Create oatpp virtual network interface for test networking
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<vsoa::network::virtual_::Interface>, virtualInterface)([] {
    return vsoa::network::virtual_::Interface::obtainShared("virtualhost");
  }());

  /**
   * Create server ConnectionProvider of oatpp virtual connections for test
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<vsoa::network::ServerConnectionProvider>, serverConnectionProvider)([] {
    VSOA_COMPONENT(std::shared_ptr<vsoa::network::virtual_::Interface>, interface);
    return vsoa::network::virtual_::server::ConnectionProvider::createShared(interface);
  }());

  /**
   * Create client ConnectionProvider of oatpp virtual connections for test
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<vsoa::network::ClientConnectionProvider>, clientConnectionProvider)([] {
    VSOA_COMPONENT(std::shared_ptr<vsoa::network::virtual_::Interface>, interface);
    return vsoa::network::virtual_::client::ConnectionProvider::createShared(interface);
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
    return vsoa::web::server::HttpConnectionHandler::createShared(router);
  }());

  /**
   *  Create ObjectMapper component to serialize/deserialize DTOs in Contoller's API
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<vsoa::data::mapping::ObjectMapper>, apiObjectMapper)([] {
    return vsoa::parser::json::mapping::ObjectMapper::createShared();
  }());

};


#endif // TestComponent_hpp
