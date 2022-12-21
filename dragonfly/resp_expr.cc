// Copyright 2023, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#include "resp_expr.h"

#include "glog/logging.h"

namespace facade {

void RespExpr::VecToArgList(const Vec& src, CmdArgVec* dest) {
    dest->resize(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        DCHECK(src[i].type == RespExpr::STRING);

        auto span = src[i].GetBuf();
        (*dest)[i] = MutableSlice{reinterpret_cast<char*>(span.data()), span.size()};
    }
}

} // namespace facade
