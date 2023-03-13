#include <libcouchbase/couchbase.h>
#include <iostream>
#include <cstring>

extern "C" {
static void
query_callback(__unused lcb_INSTANCE *instance, __unused int cbtype, const lcb_RESPN1QL *resp)
{
    // This might also fail if the index is already created!
    if (resp->rc != LCB_SUCCESS) {
        fprintf(stderr, "Query failed (%s)\n", lcb_strerror(NULL, resp->rc));
    }
    printf("Result text: %.*s\n", (int) resp->nrow, resp->row);
}
}

static void
die(const char *msg, lcb_STATUS err)
{
    std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << std::endl;
    exit(EXIT_FAILURE);
}

int
main(int, char **)
{
    lcb_STATUS rc;
    std::string connection_string = "couchbase://localhost/travel-sample";
    std::string username = "some-user";
    std::string password = "some-password";

    lcb_CREATEOPTS *create_options = nullptr;
    lcb_createopts_create(&create_options, LCB_TYPE_BUCKET);
    lcb_createopts_connstr(create_options, connection_string.data(), connection_string.size());
    lcb_createopts_credentials(create_options, username.data(), username.size(), password.data(),
            password.size());

    lcb_INSTANCE *instance;
    rc = lcb_create(&instance, create_options);
    lcb_createopts_destroy(create_options);
    if (rc != LCB_SUCCESS) {
        die("Couldn't create couchbase instance", rc);
    }

    rc = lcb_connect(instance);
    if (rc != LCB_SUCCESS) {
        die("Couldn't schedule connection", rc);
    }

    lcb_wait(instance, LCB_WAIT_DEFAULT);

    rc = lcb_get_bootstrap_status(instance);
    if (rc != LCB_SUCCESS) {
        die("Couldn't bootstrap from cluster", rc);
    }

    lcb_N1QLPARAMS *params = lcb_n1p_new();
    rc = lcb_n1p_setstmtz(params, "CREATE PRIMARY INDEX ON `travel-sample`");

    lcb_CMDN1QL cmd;
    cmd.callback = query_callback;
    lcb_n1p_mkcmd(params, &cmd);
    rc = lcb_n1ql_query(instance, nullptr, &cmd); // Check RC
    if (rc != LCB_SUCCESS) {
        die("Couldn't schedule query command", rc);
    }

    lcb_wait(instance, LCB_WAIT_DEFAULT);

    lcb_n1p_free(params);
    lcb_destroy(instance);
}
