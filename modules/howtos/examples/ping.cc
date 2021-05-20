#include <string>
#include <iostream>

#include <libcouchbase/couchbase.h>



static 
// tag::ping[]
void ping_callback(lcb_INSTANCE *, int, const lcb_RESPPING *resp)
{
    lcb_STATUS rc = lcb_respping_status(resp);
    if (rc != LCB_SUCCESS) {
        fprintf(stderr, "failed: %s\n", lcb_strerror_short(rc));
    } else {
        const char *json;
        size_t njson;
        lcb_respping_value(resp, &json, &njson);
        while (njson > 1 && json[njson - 1] == '\n') {
            njson--;
        }
        if (njson) {
            printf("%.*s", int(njson), json);
        }
    }
}
// end::ping[]


static void
check(lcb_STATUS err, const char* msg)
{
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

int
main()
{
    std::string username{ "admin" };
    std::string password{ "password" };
    std::string connection_string{ "couchbase://localhost" };

    lcb_CREATEOPTS* create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_CLUSTER), "build options object for lcb_create in CLUSTER mode");
    check(lcb_createopts_credentials(create_options, username.c_str(), username.size(), password.c_str(), password.size()),
          "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.c_str(), connection_string.size()), "assign connection string");

    lcb_INSTANCE* instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    lcb_install_callback(instance, LCB_CALLBACK_PING, reinterpret_cast<lcb_RESPCALLBACK>(ping_callback));

    lcb_CMDPING *cmd;
    lcb_cmdping_create(&cmd);
    lcb_cmdping_all(cmd);

    lcb_ping(instance, nullptr, cmd);
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    lcb_destroy(instance);

    return 0;
}
