#pragma once

namespace flame {
namespace db {
namespace kafka {
	
	class consumer;
	class message: public php::class_base {
	public:
		void init(rd_kafka_message_t* message, consumer* c);
		php::value to_string(php::parameters& params);
		php::value timestamp_ms(php::parameters& params);
		php::value timestamp(php::parameters& params);
		php::value __destruct(php::parameters& params);
	private:
		rd_kafka_message_t* msg_;
		php::object         ref_;
		consumer*           consumer_;
		int64_t             ts_;
		friend class consumer_implement;
	};
}
}
}
