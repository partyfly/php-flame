#include "../../coroutine.h"
#include "../../thread_worker.h"
#include "mysql.h"
#include "client_implement.h"
#include "client.h"
#include "result_set.h"

#define MYSQL_PING_INTERVAL 10000
namespace flame {
namespace db {
namespace mysql {
	void client::val_to_buffer(php::value& val, php::buffer& buf) {
		if(val.is_null()) {
			std::memcpy(buf.put(4), "NULL", 4);
		}else if(val.is_bool()) {
			if(val.is_true()) std::memcpy(buf.put(4), "TRUE", 4);
			else std::memcpy(buf.put(5), "FALSE", 5);
		}else if(val.is_string()) {
			buf.add('\'');
			php::string& str = val;
			char*        esp = buf.rev(str.length() * 2);
			size_t       len = mysqlnd_real_escape_string(impl->mysql_, esp,
				str.data(), str.length());
			buf.adv(len);
			buf.add('\'');
		}else if(val.is_long()) {
			long   x = val;
			size_t n = sprintf(buf.rev(10), "%ld", x);
			buf.adv(n);
		}else if(val.is_array()) {
			php::array& row = val;
			int j = -1;
			for(auto i=row.begin(); i!= row.end(); ++i) {
				if(++j == 0) buf.add('(');
				else buf.add(',');
				val_to_buffer(i->second, buf);
			}
			buf.add(')');
		}else{
			buf.add('\'');
			php::string& str = val.to_string();
			char*        esp = buf.rev(str.length() * 2);
			size_t       len = mysqlnd_real_escape_string(impl->mysql_, esp,
				str.data(), str.length());
			buf.adv(len);
			buf.add('\'');
		}
	}
	void client::key_to_buffer(php::array& map, php::buffer& buf) {
		int j = -1;
		for(auto i= map.begin(); i!= map.end(); ++i) {
			php::string& key = i->first.to_string();
			if(++j > 0) buf.add(',');
			else buf.add('(');
			buf.add('`');
			std::memcpy(buf.put(key.length()), key.data(), key.length());
			buf.add('`');
		}
		buf.add(')');
	}
	client::client()
	: impl(new client_implement(std::make_shared<thread_worker>(), this)) {
		
	}
	client::~client() {
		client_request_t* ctx = new client_request_t {
			nullptr, impl, nullptr
		};
		ctx->req.data = ctx;
		impl->worker_->queue_work(&ctx->req, client_implement::close_wk, default_cb);
	}
	php::value client::__construct(php::parameters& params) {
		if(params.length() > 0 && params[0].is_array()) {
			php::array& opts = params[0];
			if(opts.at("debug", 5).is_true()) {
				impl->debug_ = true;
			}
			php::value ping = opts.at("ping", 4);
			if(ping.is_long()) {
				impl->ping_interval = static_cast<int>(ping);
				if(impl->ping_interval < 15000) {
					impl->ping_interval = 15 * 1000;
				}
			} // 默认 ping_interval = 60 * 1000
		}
		return nullptr;
	}
	php::value client::connect(php::parameters& params) {
		client_request_t* ctx;
		if(params.length() > 0 && params[0].is_string()) {
			ctx = new client_request_t {
				coroutine::current, impl, nullptr, params[0]
			};
		}else{ // 不指定连接字符串时一般时重新连接
			ctx = new client_request_t {
				coroutine::current, impl, nullptr
			};
		}
		ctx->req.data = ctx;
		impl->worker_->queue_work(&ctx->req, client_implement::connect_wk, default_cb);
		return flame::async(this);
	}
	php::value client::format(php::parameters& params) {
		if(params.length() < 1 || !params[0].is_string()) {
			throw php::exception("format string is required");
		}
		php::string sql = params[0];
		int         idx = 0;
		php::buffer buf;
		for(char* c = sql.data(); c != sql.data() + sql.length(); ++c) {
			if(*c == '?') {
				++idx;
				val_to_buffer(params[idx], buf);
			}else{
				buf.add(*c);
			}
		}
		return std::move(buf);
	}
	void client::query_(const php::string& sql) {
		client_request_t* ctx = new client_request_t {
			coroutine::current, impl, nullptr, sql
		};
		ctx->req.data = ctx;
		impl->worker_->queue_work(&ctx->req, client_implement::query_wk, queue_cb);
	}
	void client::queue_cb(uv_work_t* req, int status) {
		client_request_t* ctx = reinterpret_cast<client_request_t*>(req->data);
		if(ctx->rv.is_pointer()) {
			MYSQLND_RES* rs = ctx->rv.ptr<MYSQLND_RES>();
			php::object obj = php::object::create<result_set>();
			result_set* cpp = obj.native<result_set>();
			ctx->rv = std::move(obj);
			cpp->init(ctx->self->worker_, ctx->self->client_, rs);
		}
		ctx->co->next(ctx->rv);
		delete ctx;
	}
	php::value client::query(php::parameters& params) {
		if(params.length() < 1 || !params[0].is_string()) {
			throw php::exception("sql string is required");
		}
		php::string sql = params[0];
		if(params.length() > 1) {
			sql = format(params);
		}
		query_(sql);
		return flame::async(this);
	}
	php::value client::insert(php::parameters& params) {
		if(params.length() < 2 || !params[0].is_string() || !params[1].is_array()) {
			throw php::exception("table name and insert data array is required");
		}
		php::string& table = params[0];
		php::array&  data  = params[1];

		php::buffer sql;
		std::memcpy(sql.put(13), "INSERT INTO `", 13);
		std::memcpy(sql.put(table.length()), table.c_str(), table.length());
		sql.add('`');
		sql.add(' ');
		if(data.is_a_map()) {
			key_to_buffer(data, sql);
			std::memcpy(sql.put(8), " VALUES ", 8);
			val_to_buffer(data, sql);
		} else {
			key_to_buffer(data[0], sql);
			std::memcpy(sql.put(8), " VALUES ", 8);
			int j = -1;
			for(auto i= data.begin(); i!= data.end(); ++i) {
				if(++j > 0) sql.add(',');
				val_to_buffer(i->second, sql);
			}
		}
		query_(std::move(sql));
		return flame::async(this);
	}
	php::value client::remove(php::parameters& params) {
		if(params.length() < 2 || !params[0].is_string() || !params[1].is_array()) {
			throw php::exception("table name and filter array is required");
		}
		php::string& table = params[0];
		php::buffer sql;
		std::memcpy(sql.put(13), "DELETE FROM `", 13);
		std::memcpy(sql.put(table.length()), table.c_str(), table.length());
		sql.add('`');
		sql_where(this, params[1], sql);
		if(params.length() > 2) {
			sql_orderby(this, params[2], sql);
		}
		if(params.length() > 3) {
			sql_limit(this, params[3], sql);
		}
		query_(std::move(sql));
		return flame::async(this);
	}
	php::value client::update(php::parameters& params) {
		if(params.length() < 2 || !params[0].is_string() || !params[1].is_array() || !params[2].is_array()) {
			throw php::exception("table name, filter array and update array is required");
		}
		php::string& table = params[0];
		php::array&  data  = params[2];
		php::buffer  sql;
		std::memcpy(sql.put(8), "UPDATE `", 8);
		std::memcpy(sql.put(table.length()), table.c_str(), table.length());
		std::memcpy(sql.put(7), "` SET", 5);
		int j = -1;
		for(auto i=data.begin(); i!=data.end(); ++i) {
			php::string& key = i->first;
			if(++j == 0) sql.add(' ');
			else sql.add(',');
			sql.add('`');
			std::memcpy(sql.put(key.length()), key.c_str(), key.length());
			sql.add('`');
			sql.add('=');
			val_to_buffer(i->second, sql);
		}
		sql_where(this, params[1], sql);
		if(params.length() > 3) {
			sql_orderby(this, params[3], sql);
		}
		if(params.length() > 4) {
			sql_limit(this, params[4], sql);
		}
		query_(std::move(sql));
		return flame::async(this);
	}
	php::value client::one(php::parameters& params) {
		if(params.length() < 2 || !params[0].is_string() || !params[1].is_array()) {
			throw php::exception("table name and query array is required");
		}
		php::string& table = params[0];
		php::buffer  sql;
		std::memcpy(sql.put(15), "SELECT * FROM `", 15);
		std::memcpy(sql.put(table.length()), table.c_str(), table.length());
		sql.add('`');
		sql_where(this, params[1], sql);
		if(params.length() > 2) {
			sql_orderby(this, params[2], sql);
		}
		std::memcpy(sql.put(8), " LIMIT 1", 8);
		client_request_t* ctx = new client_request_t {
			coroutine::current, impl, nullptr, std::move(sql)
		};
		ctx->req.data = ctx;
		impl->worker_->queue_work(&ctx->req, client_implement::one_wk, default_cb);
		return flame::async(this);
	}
	php::value client::select(php::parameters& params) {
		if(params.length() < 1 || !params[0].is_string()) {
			throw php::exception("table name is required");
		}
		php::string& table = params[0];
		php::buffer  sql;
		std::memcpy(sql.put(7), "SELECT ", 7);
		if(params.length() > 1) {
			if(params[1].is_array()) {
				php::array& col = params[1];
				int j = -1;
				for(auto i=col.begin();i!=col.end();++i) {
					php::string& str = i->second;
					if(++j > 0) sql.add(',');
					sql.add('`');
					std::memcpy(sql.put(str.length()), str.c_str(), str.length());
					sql.add('`');
				}
			}else{
				php::string& str = params[1].to_string();
				std::memcpy(sql.put(str.length()), str.c_str(), str.length());
			}
		}else{
			sql.add('*');
		}
		std::memcpy(sql.put(7), " FROM `", 7);
		std::memcpy(sql.put(table.length()), table.c_str(), table.length());
		sql.add('`');
		if(params.length() > 2) {
			sql_where(this, params[2], sql);
		}
		if(params.length() > 3) {
			sql_orderby(this, params[3], sql);
		}
		if(params.length() > 4) {
			sql_limit(this, params[4], sql);
		}
		query_(std::move(sql));
		return flame::async(this);
	}
	php::value client::found_rows(php::parameters& params) {
		client_request_t* ctx = new client_request_t {
			coroutine::current, impl, nullptr
		};
		ctx->sql = php::string("SELECT FOUND_ROWS()");
		ctx->req.data = ctx;
		impl->worker_->queue_work(&ctx->req, client_implement::found_rows_wk, default_cb);
		return flame::async(this);
	}
	void client::default_cb(uv_work_t* req, int status) {
		client_request_t* ctx = reinterpret_cast<client_request_t*>(req->data);
		if(ctx->co != nullptr) {
			// 使用 rv 可能带回错误信息
			if(ctx->rv.is_string()) {
				php::string& msg = ctx->rv;
				ctx->co->fail(msg, 0);
			}else if(ctx->rv.is_pointer() && ctx->rv.ptr<MYSQLND>() == ctx->self->mysql_) {
				ctx->co->fail(mysqlnd_error(ctx->self->mysql_), mysqlnd_errno(ctx->self->mysql_));
			}else{
				ctx->co->next(ctx->rv);
			}
		}
		delete ctx;
	}
}
}
}
