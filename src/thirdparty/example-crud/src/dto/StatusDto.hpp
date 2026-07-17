
#ifndef CRUD_STATUSDTO_HPP
#define CRUD_STATUSDTO_HPP

#include "vsoa_dto/core/macro/codegen.hpp"
#include "vsoa_dto/core/Types.hpp"

#include VSOA_CODEGEN_BEGIN(DTO)

class StatusDto : public vsoa::DTO {

  DTO_INIT(StatusDto, DTO)

  DTO_FIELD_INFO(status) {
    info->description = "Short status text";
  }
  DTO_FIELD(String, status);

  DTO_FIELD_INFO(code) {
    info->description = "Status code";
  }
  DTO_FIELD(Int32, code);

  DTO_FIELD_INFO(message) {
    info->description = "Verbose message";
  }
  DTO_FIELD(String, message);

};

#include VSOA_CODEGEN_END(DTO)

#endif //CRUD_STATUSDTO_HPP
