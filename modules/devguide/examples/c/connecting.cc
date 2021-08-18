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

static void
open_callback(lcb_INSTANCE *, lcb_STATUS rc)
{
    check(rc, "opening the bucket");
    std::cout << "The bucket has been opened successfully\n";
}

int
main()
{
    std::string username{"some-user"};
    std::string password{"some-password"};
    std::string connection_string{"couchbase://localhost"};

    lcb_CREATEOPTS *create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_CLUSTER),
            "build options object for lcb_create in CLUSTER mode");
    check(lcb_createopts_credentials(create_options, username.c_str(), username.size(),
                    password.c_str(),
                    password.size()),
            "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.c_str(),
                    connection_string.size()),
            "assign connection string");

    lcb_INSTANCE *instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    // associate instance with the bucket
    // this is equivalent for using LCB_TYPE_BUCKET and lcb_createopts_bucket() for lcb_create()
    std::string bucket_name{"default"};
    lcb_set_open_callback(instance, open_callback);
    check(lcb_open(instance, bucket_name.c_str(), bucket_name.size()), "schedule bucket opening");
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    lcb_destroy(instance);
    return 0;
}
