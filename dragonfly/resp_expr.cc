// Copyright 2023, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#include "resp_expr.h"

#include "logging.h"

namespace facade {

void RespExpr::VecToArgList(const Vec& src, CmdArgVec* dest) {
  dest->resize(src.size());
  for (size_t i = 0; i < src.size(); ++i) {
    DCHECK(src[i].type == RespExpr::STRING);

    (*dest)[i] = ToMSS(src[i].GetBuf());
  }
}

}  // namespace facade