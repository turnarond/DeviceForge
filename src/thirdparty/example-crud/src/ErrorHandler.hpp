
#ifndef CRUD_ERRORHANDLER_HPP
#define CRUD_ERRORHANDLER_HPP

#include "dto/StatusDto.hpp"

#include "oatpp/web/server/handler/ErrorHandler.hpp"
#include "oatpp/web/protocol/http/outgoing/ResponseFactory.hpp"

class ErrorHandler : public vsoa::web::server::handler::ErrorHandler {
private:
  typedef vsoa::web::protocol::http::outgoing::Response OutgoingResponse;
  typedef vsoa::web::protocol::http::Status Status;
  typedef vsoa::web::protocol::http::outgoing::ResponseFactory ResponseFactory;
private:
  std::shared_ptr<vsoa::data::mapping::ObjectMapper> m_objectMapper;
public:

  ErrorHandler(const std::shared_ptr<vsoa::data::mapping::ObjectMapper>& objectMapper);

  std::shared_ptr<OutgoingResponse>
  handleError(const Status& status, const vsoa::String& message, const Headers& headers) override;

};


#endif //CRUD_ERRORHANDLER_HPP
