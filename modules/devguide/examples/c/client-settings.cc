#include <string>
#include <iostream>
#include <chrono>

#include <libcouchbase/couchbase.h>

static void
check(lcb_STATUS err, const char* msg)
{
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

static void
open_callback(lcb_INSTANCE*, lcb_STATUS rc)
{
    check(rc, "opening the bucket");
    std::cout << "The bucket has been opened successfully\n";
}

int
main()
{
    std::string username{ "Administrator" };
    std::string password{ "password" };
    std::string connection_string{ "couchbase://localhost" };

    // settings might be specified using connection string (also see lcb_cntl_string() below)
    connection_string += "?ipv6=disabled&compression=off";

    lcb_CREATEOPTS* create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_CLUSTER), "build options object for lcb_create in CLUSTER mode");
    check(lcb_createopts_credentials(create_options, username.c_str(), username.size(), password.c_str(), password.size()),
          "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.c_str(), connection_string.size()), "assign connection string");

    lcb_INSTANCE* instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");

    // Customize client settings using lcb_cntl()
    {
        // Generic way with pointer to argument
        auto timeout_in_seconds = std::chrono::seconds(7);
        // convert timeout into microseconds for the library
        auto timeout_in_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(timeout_in_seconds);
        // cast to 32-bit number and pass pointer to lcb_cntl()
        std::uint32_t query_timeout = static_cast<std::uint32_t>(timeout_in_microseconds.count());
        check(lcb_cntl(instance, LCB_CNTL_SET, LCB_CNTL_QUERY_TIMEOUT, &query_timeout), "set default query timeout to 7 seconds");

        // alternative way for numeric arguments would be lcb_cntl_setu32()
        std::uint32_t kv_timeout = static_cast<std::uint32_t>(timeout_in_microseconds.count());
        check(lcb_cntl_setu32(instance, LCB_CNTL_OP_TIMEOUT, kv_timeout), "set default KV timeout to 7 seconds");
    }

    // lcb_cntl_string() allows to apply options using connection string syntax
    {
        check(lcb_cntl_string(instance, "analytics_timeout", "47.7"), "set analytics timeout to 47 seconds and 700 milliseconds");
    }


    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    // associate instance with the bucket
    // this is equivalent for using LCB_TYPE_BUCKET and lcb_createopts_bucket() for lcb_create()
    std::string bucket_name{ "default" };
    lcb_set_open_callback(instance, open_callback);
    check(lcb_open(instance, bucket_name.c_str(), bucket_name.size()), "schedule bucket opening");
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    lcb_destroy(instance);
    return 0;
}
