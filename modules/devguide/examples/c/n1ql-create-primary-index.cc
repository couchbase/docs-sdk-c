#include <libcouchbase/couchbase.h>
#include <libcouchbase/ixmgmt.h>
#include <iostream>
#include <cstring>

static void
die(const char *msg, lcb_STATUS err)
{
    std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << std::endl;
    exit(EXIT_FAILURE);
}

static void
ixmgmt_callback(__unused lcb_INSTANCE *instance, __unused int cbtype,
        const struct lcb_RESPN1XMGMT_st *resp)
{
    if (resp->rc == LCB_SUCCESS) {
        std::cout << "Index was successfully created!" << std::endl;
    } else if (resp->rc == LCB_ERR_INDEX_EXISTS) {
        std::cout << "Index already exists!" << std::endl;
    } else {
        std::cout << "Operation failed: " << lcb_strerror_long(resp->rc) << std::endl;
    }
}

int
main(int, char **)
{
    lcb_STATUS rc;
    std::string connection_string = "couchbase://localhost/beer-sample";
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

    const char *bktname;
    lcb_cntl(instance, LCB_CNTL_GET, LCB_CNTL_BUCKETNAME, &bktname);

    lcb_CMDN1XMGMT cmd = {};
    cmd.spec.flags = LCB_N1XSPEC_F_PRIMARY;
    cmd.spec.keyspace = bktname;
    cmd.spec.nkeyspace = strlen(bktname);
    cmd.callback = ixmgmt_callback;
    lcb_n1x_create(instance, nullptr, &cmd);
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    lcb_destroy(instance);
}
