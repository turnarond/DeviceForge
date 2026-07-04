
#ifndef CRUD_USERDB_HPP
#define CRUD_USERDB_HPP

#include "dto/UserDto.hpp"
#include "oatpp-sqlite/orm.hpp"
#include "lwcomm/lwcomm.h"

#include VSOA_CODEGEN_BEGIN(DbClient) //<- Begin Codegen

/**
 * UserDb client definitions.
 */
class UserDb : public vsoa::orm::DbClient {
public:

  UserDb(const std::shared_ptr<vsoa::orm::Executor>& executor)
    : vsoa::orm::DbClient(executor)
  {

    vsoa::orm::SchemaMigration migration(executor);
    std::string database_migrations = LWComm::GetDataPath();;
    migration.addFile(1 /* start from version 1 */, database_migrations + LW_OS_DIR_SEPARATOR + "001_init.sql");
    // TODO - Add more migrations here.
    migration.migrate(); // <-- run migrations. This guy will throw on error.

    auto version = executor->getSchemaVersion();
    VSOA_LOGD("UserDb", "Migration - OK. Version=%ld.", version);

  }

  QUERY(createUser,
        "INSERT INTO AppUser"
        "(username, email, password, role) VALUES "
        "(:user.username, :user.email, :user.password, :user.role);",
        PARAM(vsoa::Object<UserDto>, user))

  QUERY(updateUser,
        "UPDATE AppUser "
        "SET "
        " username=:user.username, "
        " email=:user.email, "
        " password=:user.password, "
        " role=:user.role "
        "WHERE "
        " id=:user.id;",
        PARAM(vsoa::Object<UserDto>, user))

  QUERY(getUserById,
        "SELECT * FROM AppUser WHERE id=:id;",
        PARAM(vsoa::Int32, id))

  QUERY(getAllUsers,
        "SELECT * FROM AppUser LIMIT :limit OFFSET :offset;",
        PARAM(vsoa::UInt32, offset),
        PARAM(vsoa::UInt32, limit))

  QUERY(deleteUserById,
        "DELETE FROM AppUser WHERE id=:id;",
        PARAM(vsoa::Int32, id))

};

#include VSOA_CODEGEN_END(DbClient) //<- End Codegen

#endif //CRUD_USERDB_HPP
