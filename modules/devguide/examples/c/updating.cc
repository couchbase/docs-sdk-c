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
    lcb_STATUS rc{LCB_SUCCESS};
    std::uint64_t cas{0};
};

static void
update_callback(lcb_INSTANCE *, int, const lcb_RESPSTORE *resp)
{
    Result *result = nullptr;
    lcb_respstore_cookie(resp, reinterpret_cast<void **>(&result));
    result->rc = lcb_respstore_status(resp);
    check(lcb_respstore_cas(resp, &result->cas), "extract CAS from STORE response");
}

int
main()
{
    std::string username{"some-user"};
    std::string password{"some-password"};
    std::string connection_string{"couchbase://localhost"};
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

    /* Set global storage callback */
    lcb_install_callback(instance, LCB_CALLBACK_STORE,
            reinterpret_cast<lcb_RESPCALLBACK>(update_callback));

    std::string key{"docid"};
    std::string value{R"({"property":"value"})"};
    Result result{};
    lcb_CMDSTORE *cmd = nullptr;

    check(lcb_cmdstore_create(&cmd, LCB_STORE_UPSERT), "create UPSERT command");
    check(lcb_cmdstore_key(cmd, key.c_str(), key.size()), "assign ID for UPSERT command");
    check(lcb_cmdstore_value(cmd, value.c_str(), value.size()), "assign value for UPSERT command");
    check(lcb_store(instance, &result, cmd), "schedule UPSERT command");
    check(lcb_cmdstore_destroy(cmd), "destroy UPSERT command");
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    std::cout << "Upsert of \"" << key << "\" got code " << lcb_strerror_short(result.rc)
              << ", CAS: " << result.cas
              << "\n";

    result = {};
    check(lcb_cmdstore_create(&cmd, LCB_STORE_INSERT), "create INSERT command");
    check(lcb_cmdstore_key(cmd, key.c_str(), key.size()), "assign ID for INSERT command");
    check(lcb_cmdstore_value(cmd, value.c_str(), value.size()), "assign value for INSERT command");
    check(lcb_store(instance, &result, cmd), "schedule INSERT command");
    check(lcb_cmdstore_destroy(cmd), "destroy INSERT command");
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    std::cout << "Insert of \"" << key << "\" got code " << lcb_strerror_short(result.rc)
              << " (error is expected)\n";

    result = {};
    check(lcb_cmdstore_create(&cmd, LCB_STORE_REPLACE), "create REPLACE command");
    check(lcb_cmdstore_key(cmd, key.c_str(), key.size()), "assign ID for REPLACE command");
    check(lcb_cmdstore_value(cmd, value.c_str(), value.size()), "assign value for REPLACE command");
    check(lcb_store(instance, &result, cmd), "schedule REPLACE command");
    check(lcb_cmdstore_destroy(cmd), "destroy REPLACE command");
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    std::cout << "Replace of \"" << key << "\" got code " << lcb_strerror_short(result.rc)
              << ", CAS: " << result.cas
              << "\n";

    lcb_destroy(instance);
    return 0;
}

// OUTPUT
//
// Upsert of "docid" got code LCB_SUCCESS (0), CAS: 1605624170540826624
// Insert of "docid" got code LCB_ERR_DOCUMENT_EXISTS (305) (error is expected)
// Replace of "docid" got code LCB_SUCCESS (0), CAS: 1605624170541285376
