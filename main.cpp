#include "common/config.h"
#include "common/log.h"
#include "common/redis.h"
#include "server/rec_server.h"

int main(int argc, char* argv[]) {
    if (!predictionmarkets_rec::config_load("./conf/config.conf")) {
        printf("config load failed\n");
        return -1;
    }

    if (!predictionmarkets_rec::log_init(predictionmarkets_rec::g_config.log.dir,
                                         predictionmarkets_rec::g_config.log.level)) {
        printf("log init failed\n");
        return -1;
    }

    if (!predictionmarkets_rec::redis::init()) {
        ALOG(FATAL, "redis init failed");
        return -1;
    }

    if (!predictionmarkets_rec::rec_server::init()) {
        ALOG(FATAL, "server init failed");
        return -1;
    }
    predictionmarkets_rec::rec_server::start();

    return 0;
}
