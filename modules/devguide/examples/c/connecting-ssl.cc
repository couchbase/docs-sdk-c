#include <libcouchbase/couchbase.h>
#include <libcouchbase/utils.h>
#include <iostream>

static void
die(lcb_STATUS rc, const char *msg) {
    std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(rc) << std::endl;
    exit(EXIT_FAILURE);
}

int
main(int, char **) {
    lcb_STATUS rc;
    std::string username = "some-user";
    std::string password = "some-password";
    std::string connection_string = "couchbases://127.0.0.1?certpath=./cluster.cert";

    lcb_CREATEOPTS *create_options = nullptr;
    lcb_createopts_create(&create_options, LCB_TYPE_CLUSTER);
    lcb_createopts_connstr(create_options, connection_string.data(), connection_string.size());
    lcb_createopts_credentials(create_options, username.data(), username.size(), password.data(), password.size());

    lcb_INSTANCE *instance;
    rc = lcb_create(&instance, create_options);
    lcb_createopts_destroy(create_options);
    if (rc != LCB_SUCCESS) {
        die(rc, "Couldn't create couchbase instance");
    }

    rc = lcb_connect(instance);
    if (rc != LCB_SUCCESS) {
        die(rc, "Couldn't schedule connection");
    }

    /* This function required to actually schedule the operations on the network */
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    /* Determines if the bootstrap/connection succeeded */
    rc = lcb_get_bootstrap_status(instance);
    if (rc != LCB_SUCCESS) {
        die(rc, "Couldn't bootstrap from cluster");
    } else {
        std::cout << "Connection succeeded. Cluster has " << lcb_get_num_nodes(instance) << " nodes" << std::endl;
    }

    /* SSL connections use different ports. For example, the REST API
     * connection will use port 18091 rather than 8091 when using SSL */
    const char *node = lcb_get_node(instance, LCB_NODE_HTCONFIG, 0);
    std::cout << "First node address for REST API: " << node << std::endl;

    lcb_destroy(instance);
    return 0;
}
