#include "common/config.h"
#include "common/log.h"
#include "common/redis.h"
#include "server/rec_server.h"

int main(int argc, char* argv[]) {
    if (!ratus_rec::config_load("./conf/config.conf")) {
        printf("config load failed\n");
        return -1;
    }

    // log_init(g_config.log.dir, g_config.log.level);

    if (!ratus_rec::redis::init()) {
        ALOG(FATAL, "redis init failed");
        return -1;
    }

    if (!ratus_rec::rec_server::init()) {
        ALOG(FATAL, "server init failed");
        return -1;
    }
    ratus_rec::rec_server::start();

    return 0;
}
