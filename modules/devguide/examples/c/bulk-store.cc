#include <vector>
#include <map>
#include <string>
#include <iostream>

#include <libcouchbase/couchbase.h>

static void
check(lcb_STATUS err, const char *msg)
{
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

struct Result {
    lcb_STATUS rc;
    std::string key{};
    std::uint64_t cas{0};

    explicit Result(const lcb_RESPSTORE *resp)
    {
        rc = lcb_respstore_status(resp);
        const char *buf = nullptr;
        std::size_t buf_len = 0;
        check(lcb_respstore_key(resp, &buf, &buf_len), "extract key from UPSERT response");
        key.assign(buf, buf_len);
        if (rc == LCB_SUCCESS) {
            check(lcb_respstore_cas(resp, &cas), "extract CAS from UPSERT response");
        }
    }
};

static void
upsert_callback(lcb_INSTANCE *, int, const lcb_RESPSTORE *resp)
{
    std::vector<Result> *results = nullptr;
    lcb_respstore_cookie(resp, reinterpret_cast<void **>(&results));
    results->emplace_back(resp);
}

int
main()
{
    std::string connection_string{"couchbase://localhost"};
    std::string username{"some-user"};
    std::string password{"some-password"};
    std::string bucket_name{"default"};

    lcb_CREATEOPTS *create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_BUCKET),
            "build options object for lcb_create");
    check(lcb_createopts_credentials(create_options, username.c_str(), username.size(),
                    password.c_str(),
                    password.size()),
            "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.c_str(),
                    connection_string.size()),
            "assign connection string");
    check(lcb_createopts_bucket(create_options, bucket_name.c_str(), bucket_name.size()),
            "assign bucket name");

    lcb_INSTANCE *instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    lcb_install_callback(instance, LCB_CALLBACK_STORE,
            reinterpret_cast<lcb_RESPCALLBACK>(upsert_callback));

    // Make a list of keys to store initially
    std::map<std::string, std::string> documents_to_store;
    documents_to_store["foo"] = R"({"value":"fooValue"})";
    documents_to_store["bar"] = R"({"value":"barValue"})";
    documents_to_store["baz"] = R"({"value":"bazValue"})";

    std::vector<Result> results;
    results.reserve(documents_to_store.size());

    lcb_sched_enter(instance);
    for (const auto &doc : documents_to_store) {
        lcb_CMDSTORE *cmd = nullptr;
        check(lcb_cmdstore_create(&cmd, LCB_STORE_UPSERT), "create UPSERT command");
        check(lcb_cmdstore_key(cmd, doc.first.c_str(), doc.first.size()),
                "assign ID for UPSERT command");
        check(lcb_cmdstore_value(cmd, doc.second.c_str(), doc.second.size()),
                "assign value for UPSERT command");
        lcb_STATUS rc = lcb_store(instance, &results, cmd);
        check(lcb_cmdstore_destroy(cmd), "destroy UPSERT command");
        if (rc != LCB_SUCCESS) {
            std::cerr << "[ERROR] could not schedule UPSERT for " << doc.first << ": "
                      << lcb_strerror_short(rc)
                      << "\n";
            // Discards all operations since the last scheduling context (created by lcb_sched_enter)
            lcb_sched_fail(instance);
            break;
        }
    }
    lcb_sched_leave(instance);
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    for (auto &result : results) {
        std::cout << result.key << ": ";
        if (result.rc != LCB_SUCCESS) {
            std::cout << "failed with error " << lcb_strerror_short(result.rc) << "\n";
        } else {
            std::cout << "CAS=" << result.cas << "\n";
        }
    }

    lcb_destroy(instance);
    return 0;
}
