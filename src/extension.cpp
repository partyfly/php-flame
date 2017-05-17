#include "vendor.h"
#include "core.h"
#include "task_runner.h"
#include "keeper.h"
#include "net/init.h"

extern "C" {
	ZEND_DLEXPORT zend_module_entry* get_module() {
		static php::extension_entry extension(EXTENSION_NAME, EXTENSION_VERSION);
		core::init(extension);
		task_runner::init(extension);
		keeper::init(extension);
		net::init(extension);
		return extension;
	}
}
