#include "../../fiber.h"



// 所有导出到 PHP 的函数必须符合下面形式：
// php::value fn(php::parameters& params);
namespace flame {
namespace net {
namespace http {
size_t write_callback(char* ptr, size_t size, size_t nmemb, void *userdata);

class client;

typedef struct curl_context_s {
	curl_context_s():sockfd(-1),cli(nullptr) {}
	uv_poll_t poll_handle;
	curl_socket_t sockfd;
	client* cli;
	php::object req;
	php::string result;
	flame::fiber* fiber;
} curl_context_t;

struct request: public php::class_base {
	request():curl_(nullptr),slist_(nullptr) {
	}
	virtual ~request() {
	}
	php::value __construct(php::parameters& params);
	php::value __destruct(php::parameters& params) {
		if (slist_) {
			curl_slist_free_all(slist_);
			slist_ = nullptr;
		}
	}

	curl_context_t* parse(client*);
	php::string& parse_form(php::array& arr);
	// 设置post数据
	php::value content(php::parameters& params) {
		php::string& content = params[0];
		prop("post") = content;
		return nullptr;
	}

	php::string get_method() {
		php::value& p = prop("method");
		return p;
	}
	curl_slist* get_header() {
		if (!slist_) {
			php::value p = prop("header");
			if (p.is_null()) {
				return nullptr;
			} else {
				php::array& header = p;
				if(header.is_empty() == false) {
					std::string str;
					for(auto i=header.begin();i!=header.end();++i) {
						php::string& key = i->first;
						php::string& value = i->second;
						if (value.length() > 0) {
							php::string str(key.length()+value.length()+2);
							strncpy(str.data(), key.c_str(), key.length());
							strncpy(str.data()+key.length(), ": ", 2);
							strncpy(str.data()+key.length()+2, value.c_str(), value.length());
							slist_ = curl_slist_append(slist_, str.c_str());
						}
					}
				}
			}
		}
		return slist_;
	}
	int get_timeout() {
		php::value& p = prop("timeout");
		if (p.is_null()) {
			return 10;
		} else {
			return p;
		}
	}
	CURL* curl_;
	curl_slist* slist_;
};

class client: public php::class_base {
public:
	client():curlm_handle_(nullptr),debug_(0){
	}
	virtual ~client() {
		release();
	}
	// curl要用的回调
	static int handle_socket(CURL* easy, curl_socket_t s, int action, void *userp, void *socketp);
	static int start_timeout(CURLM* multi, long timeout_ms, void* userp);
	static void curl_perform(uv_poll_t *req, int status, int events);
	
	php::value __construct(php::parameters& params);
	// 执行
	php::value exec(php::parameters& params);

	php::value debug(php::parameters& params);

	//不暴露给php的内部函数

	php::value exec(php::object& request);
	CURLM* get_curl_handle();
	void release() {
		if (curlm_handle_) {
			curl_multi_cleanup(curlm_handle_);
			curlm_handle_ = nullptr;
		}
	}

	uv_timer_t timeout_;
private:
	CURLM* curlm_handle_;
	int debug_;
};


php::value get(php::parameters& params);
php::value post(php::parameters& params);
php::value put(php::parameters& params);

}
}
}

