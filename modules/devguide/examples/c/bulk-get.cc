#include <vector>
#include <string>
#include <iostream>

#include <libcouchbase/couchbase.h>

static void
check(lcb_STATUS err, const char* msg)
{
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

struct Result {
    lcb_STATUS rc;
    std::string key{};
    std::string value{};
    std::uint64_t cas{ 0 };

    explicit Result(const lcb_RESPGET* resp)
    {
        rc = lcb_respget_status(resp);
        const char* buf = nullptr;
        std::size_t buf_len = 0;
        check(lcb_respget_key(resp, &buf, &buf_len), "extract key from GET response");
        key.assign(buf, buf_len);
        if (rc == LCB_SUCCESS) {
            check(lcb_respget_cas(resp, &cas), "extract CAS from GET response");
            buf = nullptr;
            buf_len = 0;
            check(lcb_respget_value(resp, &buf, &buf_len), "extract value from GET response");
            value.assign(buf, buf_len);
        }
    }
};

static void
get_callback(lcb_INSTANCE*, int, const lcb_RESPGET* resp)
{
    std::vector<Result>* results = nullptr;
    lcb_respget_cookie(resp, reinterpret_cast<void**>(&results));
    results->emplace_back(resp);
}

int
main()
{
    std::string username{ "Administrator" };
    std::string password{ "password" };
    std::string connection_string{ "couchbase://localhost" };
    std::string bucket_name{ "default" };

    lcb_CREATEOPTS* create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_BUCKET), "build options object for lcb_create");
    check(lcb_createopts_credentials(create_options, username.c_str(), username.size(), password.c_str(), password.size()),
          "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.c_str(), connection_string.size()), "assign connection string");
    check(lcb_createopts_bucket(create_options, bucket_name.c_str(), bucket_name.size()), "assign bucket name");

    lcb_INSTANCE* instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    lcb_install_callback(instance, LCB_CALLBACK_GET, reinterpret_cast<lcb_RESPCALLBACK>(get_callback));

    // Make a list of keys to store initially
    std::vector<std::string> keys_to_get{ "foo", "bar", "baz" };

    std::vector<Result> results;
    results.reserve(keys_to_get.size());

    lcb_sched_enter(instance);
    for (const auto& key : keys_to_get) {
        lcb_CMDGET* cmd = nullptr;
        check(lcb_cmdget_create(&cmd), "create GET command");
        check(lcb_cmdget_key(cmd, key.c_str(), key.size()), "assign ID for GET command");
        lcb_STATUS rc = lcb_get(instance, &results, cmd);
        check(lcb_cmdget_destroy(cmd), "destroy GET command");
        if (rc != LCB_SUCCESS) {
            std::cerr << "[ERROR] could not schedule GET for " << key << ": " << lcb_strerror_short(rc) << "\n";
            // Discards all operations since the last scheduling context (created by lcb_sched_enter)
            lcb_sched_fail(instance);
            break;
        }
    }
    lcb_sched_leave(instance);
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for batch to complete");

    for (auto& result : results) {
        std::cout << result.key << ": ";
        if (result.rc != LCB_SUCCESS) {
            std::cout << "failed with error " << lcb_strerror_short(result.rc) << "\n";
        } else {
            std::cout << "CAS=" << result.cas << ", Value:\n" << result.value << "\n";
        }
    }

    lcb_destroy(instance);
    return 0;
}
