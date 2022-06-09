// Copyright 2022, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#pragma once

#include <atomic>
#include <string>
#include <string_view>

namespace facade {

std::string WrongNumArgsError(std::string_view cmd);
std::string ConfigSetFailed(std::string_view config_name);
std::string InvalidExpireTime(std::string_view cmd);
std::string UnknownSubCmd(std::string_view subcmd, std::string_view cmd);

extern const char kSyntaxErr[];
extern const char kWrongTypeErr[];
extern const char kKeyNotFoundErr[];
extern const char kInvalidIntErr[];
extern const char kInvalidFloatErr[];
extern const char kUintErr[];
extern const char kIncrOverflow[];
extern const char kDbIndOutOfRangeErr[];
extern const char kInvalidDbIndErr[];
extern const char kScriptNotFound[];
extern const char kAuthRejected[];
extern const char kExpiryOutOfRange[];
extern const char kIndexOutOfRange[];
extern const char kOutOfMemory[];
extern const char kInvalidNumericResult[];
extern const char kClusterNotConfigured[];

extern const char kSyntaxErrType[];
extern const char kScriptErrType[];
extern const char kConfigErrType[];

const char kSyntaxErr[] = "syntax error";
const char kWrongTypeErr[] = "-WRONGTYPE Operation against a key holding the wrong kind of value";
const char kKeyNotFoundErr[] = "no such key";
const char kInvalidIntErr[] = "value is not an integer or out of range";
const char kInvalidFloatErr[] = "value is not a valid float";

const char kUintErr[] = "value is out of range, must be positive";
const char kIncrOverflow[] = "increment or decrement would overflow";
const char kDbIndOutOfRangeErr[] = "DB index is out of range";

const char kInvalidDbIndErr[] = "invalid DB index";
const char kScriptNotFound[] = "-NOSCRIPT No matching script. Please use EVAL.";
const char kAuthRejected[] = "-WRONGPASS invalid username-password pair or user is disabled.";
const char kExpiryOutOfRange[] = "expiry is out of range";
const char kIndexOutOfRange[] = "index out of range";
const char kOutOfMemory[] = "Out of memory";

const char kInvalidNumericResult[] = "result is not a number";
const char kClusterNotConfigured[] = "Cluster is not yet configured";

const char kSyntaxErrType[] = "syntax_error";
const char kScriptErrType[] = "script_error";
const char kConfigErrType[] = "config_error";

}  // namespace facade
