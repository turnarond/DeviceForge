#ifndef UserDto_hpp
#define UserDto_hpp

#include "vsoa_dto/core/macro/codegen.hpp"
#include "vsoa_dto/core/Types.hpp"

#include VSOA_CODEGEN_BEGIN(DTO)

ENUM(Role, v_int32,
     VALUE(GUEST, 0, "ROLE_GUEST"),
     VALUE(ADMIN, 1, "ROLE_ADMIN")
)

class UserDto : public vsoa::DTO {
  
  DTO_INIT(UserDto, DTO)

  DTO_FIELD(Int32, id);
  DTO_FIELD(String, userName, "username");
  DTO_FIELD(String, email, "email");
  DTO_FIELD(String, password, "password");
  DTO_FIELD(Enum<Role>::AsString, role, "role");

};

#include VSOA_CODEGEN_END(DTO)

#endif /* UserDto_hpp */
