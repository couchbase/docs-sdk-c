#include <libcouchbase/couchbase.h>
#include <vector>
#include <string>
#include <iostream>

static void
check(lcb_STATUS err, const char *msg) {
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

struct Rows {
    std::vector<std::string> rows{};
    std::string metadata{};
};

static void
query_callback(lcb_INSTANCE *, int, const lcb_RESPQUERY *resp) {
    lcb_STATUS status = lcb_respquery_status(resp);
    if (status != LCB_SUCCESS) {
        const lcb_QUERY_ERROR_CONTEXT *ctx;
        lcb_respquery_error_context(resp, &ctx);

        uint32_t err_code = 0;
        lcb_errctx_query_first_error_code(ctx, &err_code);

        const char *err_msg = nullptr;
        size_t err_msg_len = 0;
        lcb_errctx_query_first_error_message(ctx, &err_msg, &err_msg_len);
        std::string error_message{};
        if (err_msg_len > 0) {
            error_message.assign(err_msg, err_msg_len);
        }

        std::cerr << "[ERROR] failed to execute query. " << error_message << " (" << err_code
                  << ")\n";
        return;
    }

    const char *buf = nullptr;
    std::size_t buf_len = 0;
    lcb_respquery_row(resp, &buf, &buf_len);
    if (buf_len > 0) {
        Rows *result = nullptr;
        lcb_respquery_cookie(resp, reinterpret_cast<void **>(&result));
        if (lcb_respquery_is_final(resp)) {
            result->metadata.assign(buf, buf_len);
        } else {
            result->rows.emplace_back(std::string(buf, buf_len));
        }
    }
}

int
main(int, char **) {
    std::string username{"some-user"};
    std::string password{"some-password"};
    std::string bucket_name{"travel-sample"};
    std::string connection_string{"couchbase://localhost"};

    lcb_CREATEOPTS *create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_BUCKET),
          "build options object for lcb_create");
    check(lcb_createopts_credentials(create_options, username.data(), username.size(),
                                     password.data(),
                                     password.size()),
          "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.data(),
                                 connection_string.size()),
          "assign connection string");
    check(lcb_createopts_bucket(create_options, bucket_name.data(), bucket_name.size()),
          "assign bucket name");

    lcb_INSTANCE *instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    Rows result{};

    // tag::query[]
    std::string statement =
            "SELECT airportname, city, country FROM `" + bucket_name
            + R"(` WHERE type="airport" AND city="New York")";

    lcb_CMDQUERY *cmd = nullptr;
    check(lcb_cmdquery_create(&cmd), "create QUERY command");
    check(lcb_cmdquery_statement(cmd, statement.data(), statement.size()),
          "assign statement for QUERY command");
    check(lcb_cmdquery_callback(cmd, query_callback), "assign callback for QUERY command");
    check(lcb_query(instance, &result, cmd), "schedule QUERY command");
    check(lcb_cmdquery_destroy(cmd), "destroy QUERY command");
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    // end::query[]

    std::cout << "Query returned " << result.rows.size() << " rows\n";
    for (const auto &row : result.rows) {
        std::cout << row << "\n";
    }
    std::cout << "\nMetadata:\n" << result.metadata << "\n";

    lcb_destroy(instance);
}

// OUTPUT
//
// Query returned 10 rows
// {"airportname":"One Police Plaza Heliport","city":"New York","country":"United States"}
// {"airportname":"East 34th Street Heliport","city":"New York","country":"United States"}
// {"airportname":"Port Authority Bus Terminal","city":"New York","country":"United States"}
// {"airportname":"Penn Station","city":"New York","country":"United States"}
// {"airportname":"Wall Street Heliport","city":"New York","country":"United States"}
// {"airportname":"West 30th St. Heliport","city":"New York","country":"United States"}
// {"airportname":"La Guardia","city":"New York","country":"United States"}
// {"airportname":"Idlewild Intl","city":"New York","country":"United States"}
// {"airportname":"All Airports","city":"New York","country":"United States"}
// {"airportname":"John F Kennedy Intl","city":"New York","country":"United States"}
//
// Metadata:
// {
// "requestID": "beb2fec6-57bf-4543-9dab-37819eec54f0",
// "clientContextID": "ad02c7228e09fb84",
// "signature": {"airportname":"json","city":"json","country":"json"},
// "results": [
// ],
// "status": "success",
// "metrics": {"elapsedTime": "4.408204ms","executionTime": "4.370484ms","resultCount": 10,"resultSize": 805,"serviceLoad": 3}
// }
