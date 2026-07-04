
#ifndef CRUD_USERSERVICE_HPP
#define CRUD_USERSERVICE_HPP

#include "db/UserDb.hpp"
#include "dto/PageDto.hpp"
#include "dto/StatusDto.hpp"

#include "oatpp/web/protocol/http/Http.hpp"
#include "oatpp/core/macro/component.hpp"

class UserService {
private:
  typedef vsoa::web::protocol::http::Status Status;
private:
  VSOA_COMPONENT(std::shared_ptr<UserDb>, m_database); // Inject database component
public:

  vsoa::Object<UserDto> createUser(const vsoa::Object<UserDto>& dto);
  vsoa::Object<UserDto> updateUser(const vsoa::Object<UserDto>& dto);
  vsoa::Object<UserDto> getUserById(const vsoa::Int32& id, const vsoa::provider::ResourceHandle<vsoa::orm::Connection>& connection = nullptr);
  vsoa::Object<PageDto<vsoa::Object<UserDto>>> getAllUsers(const vsoa::UInt32& offset, const vsoa::UInt32& limit);
  vsoa::Object<StatusDto> deleteUserById(const vsoa::Int32& id);

};

#endif //CRUD_USERSERVICE_HPP
