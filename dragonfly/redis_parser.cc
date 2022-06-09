// Copyright 2022, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//
#include "redis_parser.h"

#pragma GCC diagnostic ignored "-Wsign-conversion"

#include <absl/strings/numbers.h>

#include "logging.h"
#include "heap_size.h"

namespace facade {

using namespace std;

auto RedisParser::Parse(Buffer str, uint32_t* consumed, RespExpr::Vec* res) -> Result {
  *consumed = 0;
  res->clear();

  if (str.size() < 2) {
    return INPUT_PENDING;
  }

  if (state_ == CMD_COMPLETE_S) {
    state_ = INIT_S;
  }

  if (state_ == INIT_S) {
    InitStart(str[0], res);
  }

  if (!cached_expr_)
    cached_expr_ = res;

  while (state_ != CMD_COMPLETE_S) {
    last_consumed_ = 0;
    switch (state_) {
      case MAP_LEN_S:
      case ARRAY_LEN_S:
        last_result_ = ConsumeArrayLen(str);
        break;
      case PARSE_ARG_S:
        if (str.size() == 0 || (str.size() < 4 && str[0] != '_')) {
          last_result_ = INPUT_PENDING;
        } else {
          last_result_ = ParseArg(str);
        }
        break;
      case INLINE_S:
        DCHECK(parse_stack_.empty());
        last_result_ = ParseInline(str);
        break;
      case BULK_STR_S:
        last_result_ = ConsumeBulk(str);
        break;
      case FINISH_ARG_S:
        HandleFinishArg();
        break;
      default:
        LOG(FATAL) << "Unexpected state " << int(state_);
    }

    *consumed += last_consumed_;

    if (last_result_ != OK) {
      break;
    }
    str.remove_prefix(last_consumed_);
  }

  if (last_result_ == INPUT_PENDING) {
    StashState(res);
  } else if (last_result_ == OK) {
    DCHECK(cached_expr_);
    if (res != cached_expr_) {
      DCHECK(!stash_.empty());

      *res = *cached_expr_;
    }
  }

  return last_result_;
}

void RedisParser::InitStart(uint8_t prefix_b, RespExpr::Vec* res) {
  buf_stash_.clear();
  stash_.clear();
  cached_expr_ = res;
  parse_stack_.clear();
  last_stashed_level_ = 0;
  last_stashed_index_ = 0;

  switch (prefix_b) {
    case '$':
    case ':':
    case '+':
    case '-':
    case '_':  // Resp3 NULL
    case ',':  // Resp3 DOUBLE
      state_ = PARSE_ARG_S;
      parse_stack_.emplace_back(1, cached_expr_);  // expression of length 1.
      break;
    case '*':
    case '~':  // Resp3 SET
      state_ = ARRAY_LEN_S;
      break;
    case '%':  // Resp3 MAP
      state_ = MAP_LEN_S;
      break;
    default:
      state_ = INLINE_S;
      break;
  }
}

void RedisParser::StashState(RespExpr::Vec* res) {
  if (cached_expr_->empty() && stash_.empty()) {
    cached_expr_ = nullptr;
    return;
  }

  if (cached_expr_ == res) {
    stash_.emplace_back(new RespExpr::Vec(*res));
    cached_expr_ = stash_.back().get();
  }

  DCHECK_LT(last_stashed_level_, stash_.size());
  while (true) {
    auto& cur = *stash_[last_stashed_level_];

    for (; last_stashed_index_ < cur.size(); ++last_stashed_index_) {
      auto& e = cur[last_stashed_index_];
      if (RespExpr::STRING == e.type) {
        Buffer& ebuf = get<Buffer>(e.u);
        if (ebuf.empty() && last_stashed_index_ + 1 == cur.size())
          break;
        if (!ebuf.empty() && !e.has_support) {
          Blob blob(ebuf.size());
          memcpy(blob.data(), ebuf.data(), ebuf.size());
          ebuf = Buffer{blob.data(), blob.size()};
          buf_stash_.push_back(std::move(blob));
          e.has_support = true;
        }
      }
    }

    if (last_stashed_level_ + 1 == stash_.size())
      break;
    ++last_stashed_level_;
    last_stashed_index_ = 0;
  }
}

auto RedisParser::ParseInline(Buffer str) -> Result {
  DCHECK(!str.empty());

  uint8_t* ptr = str.begin();
  uint8_t* end = str.end();
  uint8_t* token_start = ptr;

  if (is_broken_token_) {
    while (ptr != end && *ptr > 32)
      ++ptr;

    size_t len = ptr - token_start;

    ExtendLastString(Buffer(token_start, len));
    if (ptr != end) {
      is_broken_token_ = false;
    }
  }

  auto is_finish = [&] { return ptr == end || *ptr == '\n'; };

  while (true) {
    while (!is_finish() && *ptr <= 32) {
      ++ptr;
    }
    // We do not test for \r in order to accept 'nc' input.
    if (is_finish())
      break;

    DCHECK(!is_broken_token_);

    token_start = ptr;
    while (ptr != end && *ptr > 32)
      ++ptr;

    cached_expr_->emplace_back(RespExpr::STRING);
    cached_expr_->back().u = Buffer{token_start, size_t(ptr - token_start)};
  }

  last_consumed_ = ptr - str.data();
  if (ptr == end) {  // we have not finished parsing.
    if (ptr[-1] > 32) {
      // we stopped in the middle of the token.
      is_broken_token_ = true;
    }

    return INPUT_PENDING;
  } else {
    ++last_consumed_;  // consume the delimiter as well.
  }
  state_ = CMD_COMPLETE_S;

  return OK;
}

auto RedisParser::ParseNum(Buffer str, int64_t* res) -> Result {
  if (str.size() < 4) {
    return INPUT_PENDING;
  }
  DCHECK(str[0] == '$' || str[0] == '*' || str[0] == '%');

  char* s = reinterpret_cast<char*>(str.data() + 1);
  char* pos = reinterpret_cast<char*>(memchr(s, '\n', str.size() - 1));
  if (!pos) {
    return str.size() < 32 ? INPUT_PENDING : BAD_INT;
  }
  if (pos[-1] != '\r') {
    return BAD_INT;
  }

  bool success = absl::SimpleAtoi(absl::string_view{s, size_t(pos - s - 1)}, res);
  if (!success) {
    return BAD_INT;
  }
  last_consumed_ = (pos - s) + 2;

  return OK;
}

auto RedisParser::ConsumeArrayLen(Buffer str) -> Result {
  int64_t len;

  Result res = ParseNum(str, &len);
  if (state_ == MAP_LEN_S) {
    // Map starts with %N followed by an array of 2*N elements.
    // Even elements are keys, odd elements are values.
    len *= 2;
  }
  switch (res) {
    case INPUT_PENDING:
      return INPUT_PENDING;
    case BAD_INT:
      return BAD_ARRAYLEN;
    case OK:
      if (len < -1 || len > max_arr_len_) {
        LOG_IF(WARNING, len > max_arr_len_) << "Multibulk len is too large " << len;

        return BAD_ARRAYLEN;
      }
      break;
    default:
      LOG(ERROR) << "Unexpected result " << res;
  }

  if (server_mode_ && (parse_stack_.size() > 0 || !cached_expr_->empty()))
    return BAD_STRING;

  if (len <= 0) {
    cached_expr_->emplace_back(len == -1 ? RespExpr::NIL_ARRAY : RespExpr::ARRAY);
    if (len < 0)
      cached_expr_->back().u.emplace<RespVec*>(nullptr);  // nil
    else {
      static RespVec empty_vec;
      cached_expr_->back().u = &empty_vec;
    }
    state_ = (parse_stack_.empty()) ? CMD_COMPLETE_S : FINISH_ARG_S;

    return OK;
  }

  if (state_ == PARSE_ARG_S) {
    DCHECK(!server_mode_);

    cached_expr_->emplace_back(RespExpr::ARRAY);
    stash_.emplace_back(new RespExpr::Vec());
    RespExpr::Vec* arr = stash_.back().get();
    arr->reserve(len);
    cached_expr_->back().u = arr;
    cached_expr_ = arr;
  } else {
    state_ = PARSE_ARG_S;
  }

  DVLOG(1) << "PushStack: (" << len << ", " << cached_expr_ << ")";
  parse_stack_.emplace_back(len, cached_expr_);

  return OK;
}

auto RedisParser::ParseArg(Buffer str) -> Result {
  char c = str[0];

  if (c == '$') {
    int64_t len;

    Result res = ParseNum(str, &len);
    switch (res) {
      case INPUT_PENDING:
        return INPUT_PENDING;
      case BAD_INT:
        return BAD_ARRAYLEN;
      case OK:
        if (len < -1 || len > kMaxBulkLen)
          return BAD_ARRAYLEN;
        break;
      default:
        LOG(ERROR) << "Unexpected result " << res;
    }

    if (len < 0) {  // Resp2 NIL
      state_ = FINISH_ARG_S;
      cached_expr_->emplace_back(RespExpr::NIL);
    } else {
      DVLOG(1) << "String(" << len << ")";
      cached_expr_->emplace_back(RespExpr::STRING);
      bulk_len_ = len;
      state_ = BULK_STR_S;
    }
    cached_expr_->back().u = Buffer{};

    return OK;
  }

  if (server_mode_) {
    return BAD_BULKLEN;
  }

  if (c == '_') {  // Resp3 NIL
    // TODO: Do we need to validate that str[1:2] == "\r\n"?
    state_ = FINISH_ARG_S;
    cached_expr_->emplace_back(RespExpr::NIL);
    cached_expr_->back().u = Buffer{};
    last_consumed_ += 3;  // '_','\r','\n'
    return OK;
  }

  if (c == '*') {
    return ConsumeArrayLen(str);
  }

  char* s = reinterpret_cast<char*>(str.data() + 1);
  char* eol = reinterpret_cast<char*>(memchr(s, '\n', str.size() - 1));

  if (c == '+' || c == '-') {  // Simple string or error.
    DCHECK(!server_mode_);
    if (!eol) {
      return str.size() < 256 ? INPUT_PENDING : BAD_STRING;
    }
    if (eol[-1] != '\r')
      return BAD_STRING;

    cached_expr_->emplace_back(c == '+' ? RespExpr::STRING : RespExpr::ERROR);
    cached_expr_->back().u = Buffer{reinterpret_cast<uint8_t*>(s), size_t((eol - 1) - s)};
  } else if (c == ':') {
    DCHECK(!server_mode_);
    if (!eol) {
      return str.size() < 32 ? INPUT_PENDING : BAD_INT;
    }
    int64_t ival;
    absl::string_view tok{s, size_t((eol - s) - 1)};

    if (eol[-1] != '\r' || !absl::SimpleAtoi(tok, &ival))
      return BAD_INT;

    cached_expr_->emplace_back(RespExpr::INT64);
    cached_expr_->back().u = ival;
  } else if (c == ',') {
    DCHECK(!server_mode_);
    if (!eol) {
      return str.size() < 32 ? INPUT_PENDING : BAD_DOUBLE;
    }
    double_t dval;
    absl::string_view tok{s, size_t((eol - s) - 1)};

    if (eol[-1] != '\r' || !absl::SimpleAtod(tok, &dval))
      return BAD_INT;

    cached_expr_->emplace_back(RespExpr::DOUBLE);
    cached_expr_->back().u = dval;
  } else {
    return BAD_STRING;
  }

  last_consumed_ = (eol - s) + 2;
  state_ = FINISH_ARG_S;
  return OK;
}

auto RedisParser::ConsumeBulk(Buffer str) -> Result {
  auto& bulk_str = get<Buffer>(cached_expr_->back().u);

  if (str.size() >= bulk_len_ + 2) {
    if (str[bulk_len_] != '\r' || str[bulk_len_ + 1] != '\n') {
      return BAD_STRING;
    }

    if (bulk_len_) {
      if (is_broken_token_) {
        memcpy(bulk_str.end(), str.data(), bulk_len_);
        bulk_str = Buffer{bulk_str.data(), bulk_str.size() + bulk_len_};
      } else {
        bulk_str = str.subspan(0, bulk_len_);
      }
    }
    is_broken_token_ = false;
    state_ = FINISH_ARG_S;
    last_consumed_ = bulk_len_ + 2;
    bulk_len_ = 0;

    return OK;
  }

  if (str.size() >= 32) {
    DCHECK(bulk_len_);
    size_t len = std::min<size_t>(str.size(), bulk_len_);

    if (is_broken_token_) {
      memcpy(bulk_str.end(), str.data(), len);
      bulk_str = Buffer{bulk_str.data(), bulk_str.size() + len};
      DVLOG(1) << "Extending bulk stash to size " << bulk_str.size();
    } else {
      DVLOG(1) << "New bulk stash size " << bulk_len_;
      vector<uint8_t> nb(bulk_len_);
      memcpy(nb.data(), str.data(), len);
      bulk_str = Buffer{nb.data(), len};
      buf_stash_.emplace_back(move(nb));
      is_broken_token_ = true;
      cached_expr_->back().has_support = true;
    }
    last_consumed_ = len;
    bulk_len_ -= len;
  }

  return INPUT_PENDING;
}

void RedisParser::HandleFinishArg() {
  state_ = PARSE_ARG_S;
  DCHECK(!parse_stack_.empty());
  DCHECK_GT(parse_stack_.back().first, 0u);

  while (true) {
    --parse_stack_.back().first;
    if (parse_stack_.back().first != 0)
      break;
    auto* arr = parse_stack_.back().second;
    DVLOG(1) << "PopStack (" << arr << ")";
    parse_stack_.pop_back();  // pop 0.
    if (parse_stack_.empty()) {
      state_ = CMD_COMPLETE_S;
      break;
    }
    cached_expr_ = parse_stack_.back().second;
  }
}

void RedisParser::ExtendLastString(Buffer str) {
  DCHECK(!cached_expr_->empty() && cached_expr_->back().type == RespExpr::STRING);
  DCHECK(!buf_stash_.empty());

  Buffer& last_str = get<Buffer>(cached_expr_->back().u);

  DCHECK(last_str.data() == buf_stash_.back().data());

  vector<uint8_t> nb(last_str.size() + str.size());
  memcpy(nb.data(), last_str.data(), last_str.size());
  memcpy(nb.data() + last_str.size(), str.data(), str.size());
  last_str = RespExpr::Buffer{nb.data(), last_str.size() + str.size()};
  buf_stash_.back() = std::move(nb);
}

size_t RedisParser::UsedMemory() const {
  return dfly::HeapSize(parse_stack_) + dfly::HeapSize(stash_) + dfly::HeapSize(buf_stash_);
}

}  // namespace facade
