#include <libcouchbase/couchbase.h>
#include <iostream>
#include <cstdlib>
#include <string>

static void
die(const char *msg, lcb_STATUS err) {
    std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << std::endl;
    exit(EXIT_FAILURE);
}

static void
row_callback(lcb_INSTANCE *, int, const lcb_RESPSEARCH *resp) {
    if (lcb_respsearch_is_final(resp)) {
        std::cout << "Status: " << lcb_respsearch_status(resp) << std::endl;

        const char *row;
        size_t nrow;
        lcb_respsearch_row(resp, &row, &nrow);
        std::cout << "Meta: " << std::string(row, nrow) << std::endl;

        const lcb_RESPHTTP *http_resp;
        lcb_respsearch_http_response(resp, &http_resp);
        if (http_resp) {
            const char *body;
            size_t bodyLen;
            if (lcb_resphttp_body(http_resp, &body, &bodyLen) == LCB_SUCCESS) {
                std::cout << "HTTP Response: " << std::string(body, bodyLen) << std::endl;
            }
        }
    } else {
        const char *row;
        size_t nrow;
        lcb_respsearch_row(resp, &row, &nrow);
        std::cout << "Row: " << std::string(row, nrow) << std::endl;
    }
}

int
main(int, char **) {
    lcb_STATUS rc;
    std::string username = "some-user";
    std::string password = "some-password";
    std::string connection_string = "couchbase://localhost/beer-sample";

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

    lcb_cntl_string(instance, "detailed_errcodes", "true");
    rc = lcb_connect(instance);
    if (rc != LCB_SUCCESS) {
        die("Couldn't schedule connection", rc);
    }

    lcb_wait(instance, LCB_WAIT_DEFAULT);

    rc = lcb_get_bootstrap_status(instance);
    if (rc != LCB_SUCCESS) {
        die("Couldn't bootstrap from cluster", rc);
    }

    // This example requires an FTS Search Index named `beer-search` created for the `beer-sample` bucket
    // Be sure to include the indexName within the request payload
    std::string encodedQuery(
            "{\"query\":{\"match\":\"hoppy\"},\"indexName\":\"beer-search\",\"size\":10}");

    lcb_CMDSEARCH *cmd;
    lcb_cmdsearch_create(&cmd);
    lcb_cmdsearch_callback(cmd, row_callback);
    lcb_cmdsearch_payload(cmd, encodedQuery.data(), encodedQuery.size());
    rc = lcb_search(instance, NULL, cmd);
    if (rc != LCB_SUCCESS) {
        die("Couldn't schedule FTS query", rc);
    }
    lcb_cmdsearch_destroy(cmd);

    std::cout << "----> " << encodedQuery << std::endl;

    lcb_wait(instance, LCB_WAIT_DEFAULT);
    lcb_destroy(instance);
}
