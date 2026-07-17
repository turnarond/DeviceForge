
#ifndef CRUD_STATICCONTROLLER_HPP
#define CRUD_STATICCONTROLLER_HPP

#include "oatpp/web/server/api/ApiController.hpp"
#include "vsoa_dto/parser/json/mapping/ObjectMapper.hpp"
#include "vsoa_dto/core/macro/codegen.hpp"
#include "oatpp/core/macro/component.hpp"

#include VSOA_CODEGEN_BEGIN(ApiController) //<- Begin Codegen

class StaticController : public vsoa::web::server::api::ApiController {
public:
  StaticController(const std::shared_ptr<ObjectMapper>& objectMapper)
    : vsoa::web::server::api::ApiController(objectMapper)
  {}
public:

  static std::shared_ptr<StaticController> createShared(
    VSOA_COMPONENT(std::shared_ptr<ObjectMapper>, objectMapper) // Inject objectMapper component here as default parameter
  ){
    return std::make_shared<StaticController>(objectMapper);
  }

  ENDPOINT("GET", "/", root) {
    const char* html =
      "<html lang='en'>"
      "  <head>"
      "    <meta charset=utf-8/>"
      "  </head>"
      "  <body>"
      "    <p>Hello CRUD example project!</p>"
      "    <a href='swagger/ui'>Checkout Swagger-UI page</a>"
      "  </body>"
      "</html>";
    auto response = createResponse(Status::CODE_200, html);
    response->putHeader(Header::CONTENT_TYPE, "text/html");
    return response;
  }

};

#include VSOA_CODEGEN_END(ApiController) //<- End Codegen

#endif //CRUD_STATICCONTROLLER_HPP
