#pragma once

#include <cstring>
#include <cstdlib>
#include <memory>
#include <stack>
#include <map>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "curl/include/curl/curl.h"
#include "hiredis/hiredis.h"
#include "hiredis/async.h"
#include "hiredis/adapters/libuv.h"
#include "libuv/include/uv.h"
#include "libphpext/phpext.h"
