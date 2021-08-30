#include <libcouchbase/couchbase.h>
#include <iostream>

extern "C" {
static void
query_callback(lcb_INSTANCE *, int, const lcb_RESPQUERY *resp) {
    // This might also fail if the index is already created!
    lcb_STATUS rc = lcb_respquery_status(resp);
    if (rc != LCB_SUCCESS) {
        std::cerr << "N1QL query failed: " << lcb_strerror_short(rc) << std::endl;
    }

    const char *row = nullptr;
    size_t rowLen = 0;
    lcb_respquery_row(resp, &row, &rowLen);
    if (rowLen > 0) {
        std::cout << "Query callback response: " << std::string(row, rowLen);
    } else {
        std::cout << "(Query callback response was EMPTY)";
    }
    std::cout << std::endl;
}
}

static void
die(const char *msg, lcb_STATUS err) {
    std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << std::endl;
    exit(EXIT_FAILURE);
}

int
main(int, char **) {
    lcb_STATUS rc;
    std::string username = "some-user";
    std::string password = "some-password";
    std::string connection_string = "couchbase://localhost/travel-sample";

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

    lcb_CMDQUERY *cmdQuery = nullptr;
    lcb_cmdquery_create(&cmdQuery);

    std::string queryStatement = "CREATE PRIMARY INDEX ON `travel-sample`";
    lcb_cmdquery_statement(cmdQuery, queryStatement.data(), queryStatement.size());
    lcb_cmdquery_callback(cmdQuery, query_callback);

    rc = lcb_query(instance, nullptr, cmdQuery);
    if (rc != LCB_SUCCESS) {
        die("Couldn't schedule query command", rc);
    }
    lcb_cmdquery_destroy(cmdQuery);

    lcb_wait(instance, LCB_WAIT_DEFAULT);

    lcb_destroy(instance);
}
