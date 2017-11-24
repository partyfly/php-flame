#include "../../coroutine.h"
#include "../../thread_worker.h"
#include "mysql.h"
#include "result_implement.h"
#include "result_set.h"

namespace flame {
namespace db {
namespace mysql {
	result_set::result_set()
	:impl(nullptr) {
		
	}
	void result_set::init(std::shared_ptr<thread_worker> worker, client* cli, MYSQLND_RES* rs) {
		impl = new result_implement(worker, rs);
		ref_ = cli;
	}
	result_set::~result_set() {
		if(impl) {
			result_request_t* ctx = new result_request_t {
				nullptr, impl, nullptr
			};
			ctx->req.data = ctx;
			impl->worker_->queue_work(&ctx->req, result_implement::close_wk, default_cb);
		}
	}
	php::value result_set::fetch_row(php::parameters& params) {
		result_request_t* ctx = new result_request_t {
			coroutine::current, impl, this, MYSQLND_FETCH_ASSOC
		};
		ctx->req.data = ctx;
		if(params.length() > 0 && params[0].to_long() == MYSQLND_FETCH_NUM) {
			ctx->type = MYSQLND_FETCH_NUM;
		}
		impl->worker_->queue_work(&ctx->req, result_implement::fetch_row_wk, default_cb);
		return flame::async();
	}
	php::value result_set::fetch_all(php::parameters& params) {
		result_request_t* ctx = new result_request_t {
			coroutine::current, impl, this, MYSQLND_FETCH_ASSOC
		};
		ctx->req.data = ctx;
		if(params.length() > 0 && params[0].to_long() == MYSQLND_FETCH_NUM) {
			ctx->type = MYSQLND_FETCH_NUM;
		}
		impl->worker_->queue_work(&ctx->req, result_implement::fetch_all_wk, default_cb);
		return flame::async();
	}
	void result_set::default_cb(uv_work_t* req, int status) {
		result_request_t* ctx = reinterpret_cast<result_request_t*>(req->data);
		if(ctx->co != nullptr) ctx->co->next(ctx->rv);
		delete ctx;
	}
}
}
}
