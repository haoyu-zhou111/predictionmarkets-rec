#include <cstdlib>
#include <string>

#include "common/config.h"
#include "common/log.h"
#include "common/redis.h"
#include "server/rec_server.h"

int main(int argc, char* argv[]) {
    // 默认测试环境；只有显式信号才切生产，避免误连线上
    std::string conf_path = "./conf/config.conf";
    if (argc > 1) {
        conf_path = argv[1];
    } else if (const char* env = std::getenv("APP_ENV");
               env && std::string(env) == "prod") {
        conf_path = "./conf/config.prod.conf";
    }

    if (!predictionmarkets_rec::config_load(conf_path)) {
        printf("config load failed: %s\n", conf_path.c_str());
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
