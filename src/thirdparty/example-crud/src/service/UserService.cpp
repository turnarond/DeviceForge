
#include "UserService.hpp"

vsoa::Object<UserDto> UserService::createUser(const vsoa::Object<UserDto>& dto) {

  auto dbResult = m_database->createUser(dto);
  OATPP_ASSERT_HTTP(dbResult->isSuccess(), Status::CODE_500, dbResult->getErrorMessage());

  auto userId = vsoa::sqlite::Utils::getLastInsertRowId(dbResult->getConnection());

  return getUserById((v_int32) userId);

}

vsoa::Object<UserDto> UserService::updateUser(const vsoa::Object<UserDto>& dto) {

  auto dbResult = m_database->updateUser(dto);
  OATPP_ASSERT_HTTP(dbResult->isSuccess(), Status::CODE_500, dbResult->getErrorMessage());
  return getUserById(dto->id);

}

vsoa::Object<UserDto> UserService::getUserById(const vsoa::Int32& id, const vsoa::provider::ResourceHandle<vsoa::orm::Connection>& connection) {

  auto dbResult = m_database->getUserById(id, connection);
  OATPP_ASSERT_HTTP(dbResult->isSuccess(), Status::CODE_500, dbResult->getErrorMessage());
  OATPP_ASSERT_HTTP(dbResult->hasMoreToFetch(), Status::CODE_404, "User not found");

  auto result = dbResult->fetch<vsoa::Vector<vsoa::Object<UserDto>>>();
  OATPP_ASSERT_HTTP(result->size() == 1, Status::CODE_500, "Unknown error");

  return result[0];

}

vsoa::Object<PageDto<vsoa::Object<UserDto>>> UserService::getAllUsers(const vsoa::UInt32& offset, const vsoa::UInt32& limit) {

  vsoa::UInt32 countToFetch = limit;

  if(limit > 10) {
    countToFetch = 10;
  }

  auto dbResult = m_database->getAllUsers(offset, countToFetch);
  OATPP_ASSERT_HTTP(dbResult->isSuccess(), Status::CODE_500, dbResult->getErrorMessage());

  auto items = dbResult->fetch<vsoa::Vector<vsoa::Object<UserDto>>>();

  auto page = PageDto<vsoa::Object<UserDto>>::createShared();
  page->offset = offset;
  page->limit = countToFetch;
  page->count = items->size();
  page->items = items;

  return page;

}

vsoa::Object<StatusDto> UserService::deleteUserById(const vsoa::Int32& userId) {
  auto dbResult = m_database->deleteUserById(userId);
  OATPP_ASSERT_HTTP(dbResult->isSuccess(), Status::CODE_500, dbResult->getErrorMessage());
  auto status = StatusDto::createShared();
  status->status = "OK";
  status->code = 200;
  status->message = "User was successfully deleted";
  return status;
}